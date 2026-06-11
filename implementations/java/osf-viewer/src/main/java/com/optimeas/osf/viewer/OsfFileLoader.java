// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import com.optimeas.osf.DataManager;
import javafx.concurrent.Task;

import java.nio.file.Path;

/**
 * Loads an OSF or OSFZ file into a {@link ViewerModel}.
 *
 * <p>The synchronous {@link #loadInto(Path)} method is pure and testable without
 * a JavaFX runtime. The {@link #task(Path)} factory wraps it in a
 * {@link javafx.concurrent.Task} so the UI can run it off the FX thread via a
 * {@link javafx.concurrent.Service} or a plain {@link Thread}.
 */
public final class OsfFileLoader {

    private OsfFileLoader() {}

    /**
     * Synchronously load an OSF/OSFZ file and return a populated {@link ViewerModel}.
     *
     * <p>This is the testable, non-JavaFX core: it calls
     * {@link DataManager#loadFromFile(Path)}, creates a fresh model, sets the
     * data, and returns it. No FX toolkit required.
     *
     * @param path path to the {@code .osf} or {@code .osfz} file
     * @return a fully initialised model with visible range covering all chartable data
     * @throws com.optimeas.osf.OsfException.MalformedFile if the file cannot be parsed
     */
    public static ViewerModel loadInto(Path path) {
        DataManager mgr = DataManager.loadFromFile(path);
        ViewerModel model = new ViewerModel();
        model.setData(mgr);
        return model;
    }

    /**
     * Create a JavaFX {@link Task} that loads the file off the FX thread.
     *
     * <p>Use this in the UI: wire the task's {@code onSucceeded} callback to
     * push the new model into the view, and {@code onFailed} to show the error.
     *
     * <pre>{@code
     *   Task<ViewerModel> t = OsfFileLoader.task(path);
     *   t.setOnSucceeded(e -> model.setData(t.getValue().channels()...));
     *   t.setOnFailed(e -> statusLabel.setText(t.getException().getMessage()));
     *   new Thread(t).start();
     * }</pre>
     *
     * @param path path to the {@code .osf} or {@code .osfz} file
     * @return a task that, when run, calls {@link #loadInto(Path)}
     */
    public static Task<ViewerModel> task(Path path) {
        return new Task<>() {
            @Override
            protected ViewerModel call() {
                return loadInto(path);
            }
        };
    }
}
