// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import com.optimeas.osf.DataManager;
import javafx.concurrent.Task;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.ToolBar;
import javafx.scene.layout.BorderPane;
import javafx.scene.layout.StackPane;
import javafx.stage.FileChooser;

import java.io.File;
import java.nio.file.Path;

/**
 * Top-level application layout.
 *
 * <pre>
 *  +--ToolBar (Open / Zoom Reset)-----------------------+
 *  | ChannelListView (left) | PlotCanvas (center/fill) |
 *  +--status Label (bottom)-----------------------------+
 * </pre>
 *
 * <p>The {@link PlotCanvas} lives inside a resizable {@link StackPane} so the
 * canvas fills the available space. All interaction (file open, zoom reset,
 * cursor readout) is wired here.
 */
public final class MainView extends BorderPane {

    private final ViewerModel model = new ViewerModel();
    private final PlotCanvas canvas;
    private final ChannelListView channelList;
    private final Label statusLabel = new Label("No file loaded.");

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------

    public MainView() {
        // ---- Canvas + resizable wrapper -----
        canvas = new PlotCanvas(model);

        StackPane canvasPane = new StackPane(canvas);
        // Bind the canvas size to the StackPane so it fills the center.
        canvas.widthProperty().bind(canvasPane.widthProperty());
        canvas.heightProperty().bind(canvasPane.heightProperty());

        // ---- Channel list (left) -----
        channelList = new ChannelListView(model);
        channelList.setPrefWidth(420);

        // ---- Toolbar (top) -----
        Button openBtn      = new Button("Open…");
        Button zoomResetBtn = new Button("Zoom Reset");

        openBtn.setOnAction(e -> handleOpen());
        zoomResetBtn.setOnAction(e -> handleZoomReset());

        ToolBar toolBar = new ToolBar(openBtn, zoomResetBtn);

        // ---- Cursor readout (bottom status) -----
        canvas.setCursorListener(text -> statusLabel.setText(text));

        // ---- Layout -----
        setTop(toolBar);
        setLeft(channelList);
        setCenter(canvasPane);
        setBottom(statusLabel);
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /**
     * The shared viewer model. Exposed so {@link ViewerApp} can pre-populate
     * it when a file path is passed as a launch parameter.
     */
    public ViewerModel model() {
        return model;
    }

    /**
     * Open a specific file programmatically (e.g. from a launch argument).
     *
     * @param path path to the {@code .osf} or {@code .osfz} file
     */
    public void openFile(Path path) {
        statusLabel.setText("Loading " + path.getFileName() + "…");
        runLoaderTask(path);
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    private void handleOpen() {
        FileChooser chooser = new FileChooser();
        chooser.setTitle("Open OSF File");
        chooser.getExtensionFilters().addAll(
                new FileChooser.ExtensionFilter("OSF files", "*.osf", "*.osfz"),
                new FileChooser.ExtensionFilter("All files", "*.*"));

        // getScene().getWindow() may be null during early startup — guard.
        File file = (getScene() != null)
                ? chooser.showOpenDialog(getScene().getWindow())
                : chooser.showOpenDialog(null);

        if (file == null) return; // user cancelled

        statusLabel.setText("Loading " + file.getName() + "…");
        runLoaderTask(file.toPath());
    }

    private void handleZoomReset() {
        // Reset the visible range back to the full data extent.
        // The easiest way is to re-trigger setData without re-loading; for that
        // we just re-apply the cached model data extent. Because ViewerModel
        // does not expose a "reset zoom" method directly, we call setVisibleRange
        // with the widened time extents derived from the model itself.
        //
        // The model initialises t0/t1 from all chartable channels in setData.
        // We track it indirectly: find the min first-timestamp and max last-timestamp
        // across all selected (or all chartable) channels by re-scanning the model.
        //
        // Simplest correct approach: reload from nothing is not available without
        // re-reading the file. Instead we collapse back to the original full range
        // by using a sentinel approach: if no channels are loaded, nothing to do.
        if (model.channels().isEmpty()) return;

        // Derive the full extent from all chartable channels in the model.
        long globalMin = Long.MAX_VALUE;
        long globalMax = Long.MIN_VALUE;
        for (var ch : model.channels()) {
            if (!ViewerModel.isChartable(ch)) continue;
            try {
                long[] ts = model.timestampsNsFor(ch.name());
                if (ts.length == 0) continue;
                if (ts[0] < globalMin) globalMin = ts[0];
                if (ts[ts.length - 1] > globalMax) globalMax = ts[ts.length - 1];
            } catch (IllegalArgumentException ignored) {
                // channel not in cache — skip
            }
        }

        if (globalMin < globalMax) {
            model.setVisibleRange(globalMin, globalMax);
        }
    }

    private void runLoaderTask(Path path) {
        // Load in background; deliver a DataManager so we can call model.setData
        // on the FX thread without a second file read.
        Task<DataManager> loaderTask = new Task<>() {
            @Override
            protected DataManager call() {
                return DataManager.loadFromFile(path);
            }
        };

        loaderTask.setOnSucceeded(e -> {
            // Back on the FX thread (guaranteed by Task).
            DataManager mgr = loaderTask.getValue();
            model.setData(mgr);
            channelList.refresh(model);
            canvas.render();
            statusLabel.setText("Loaded: " + path.getFileName()
                    + " (" + model.channels().size() + " channels)");
        });

        loaderTask.setOnFailed(e -> {
            Throwable ex = loaderTask.getException();
            statusLabel.setText("Error: " + (ex != null ? ex.getMessage() : "unknown error"));
        });

        Thread t = new Thread(loaderTask);
        t.setDaemon(true);
        t.start();
    }
}
