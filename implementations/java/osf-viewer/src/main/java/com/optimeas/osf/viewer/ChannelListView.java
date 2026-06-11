// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import com.optimeas.osf.DataChannel;
import com.optimeas.osf.DataType;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleObjectProperty;
import javafx.collections.FXCollections;
import javafx.scene.control.CheckBox;
import javafx.scene.control.TableCell;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableView;
import javafx.scene.control.Tooltip;
import javafx.beans.property.ReadOnlyStringWrapper;

import java.util.HashMap;
import java.util.Map;

/**
 * A {@link TableView} showing the OSF channels in the loaded file.
 *
 * <p>Columns: checkbox (select/deselect for plotting), name, datatype, mode
 * (kind), samples, and unit. The checkbox is disabled (and rendered as a
 * grayed-out unchecked box with a tooltip "not plotted in v1") for
 * non-chartable channels — those with data type STRING, BINARY, GPS_LOCATION,
 * or UNSUPPORTED.
 *
 * <p>Call {@link #refresh(ViewerModel)} when a new file is loaded to rebuild
 * the item list and reset all checkboxes.
 */
public final class ChannelListView extends TableView<DataChannel> {

    /**
     * Per-row checkbox state. Keyed by channel name, so it survives the rebuild
     * in {@link #refresh(ViewerModel)} only within one model lifetime (cleared
     * on each refresh).
     */
    private final Map<String, SimpleBooleanProperty> checkboxStates = new HashMap<>();

    /**
     * The active viewer model. Updated in {@link #refresh(ViewerModel)}.
     */
    private ViewerModel model;

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------

    /**
     * Create a {@code ChannelListView} bound to {@code model}.
     *
     * @param model the viewer model (must not be {@code null})
     */
    public ChannelListView(ViewerModel model) {
        this.model = model;
        buildColumns();
        setItems(FXCollections.observableArrayList(model.channels()));
        setColumnResizePolicy(CONSTRAINED_RESIZE_POLICY_FLEX_LAST_COLUMN);
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /**
     * Rebuild the table after a new file is loaded into {@code model}.
     *
     * <p>Clears all checkbox state, repopulates the item list, and rebinds
     * all cell-value factories to the new model.
     *
     * @param model the updated viewer model
     */
    public void refresh(ViewerModel model) {
        this.model = model;
        checkboxStates.clear();
        setItems(FXCollections.observableArrayList(model.channels()));
    }

    // -------------------------------------------------------------------------
    // Column construction
    // -------------------------------------------------------------------------

    @SuppressWarnings("unchecked")
    private void buildColumns() {

        // --- Checkbox column -------------------------------------------------
        TableColumn<DataChannel, Boolean> selectCol = new TableColumn<>("Plot");
        selectCol.setMinWidth(40);
        selectCol.setPrefWidth(48);
        selectCol.setMaxWidth(56);
        selectCol.setSortable(false);

        selectCol.setCellValueFactory(cd -> {
            DataChannel ch = cd.getValue();
            return checkboxProperty(ch);
        });

        selectCol.setCellFactory(col -> new TableCell<>() {
            private final CheckBox checkBox = new CheckBox();

            {
                checkBox.setOnAction(evt -> {
                    DataChannel ch = getTableView().getItems().get(getIndex());
                    boolean on = checkBox.isSelected();
                    if (model != null) {
                        model.setSelected(ch.name(), on);
                    }
                    // Keep the backing property in sync.
                    checkboxProperty(ch).set(on);
                });
            }

            @Override
            protected void updateItem(Boolean item, boolean empty) {
                super.updateItem(item, empty);
                if (empty || getIndex() < 0 || getIndex() >= getTableView().getItems().size()) {
                    setGraphic(null);
                    return;
                }
                DataChannel ch = getTableView().getItems().get(getIndex());
                boolean chartable = isChartable(ch);

                checkBox.setSelected(item != null && item);
                checkBox.setDisable(!chartable);
                if (!chartable) {
                    checkBox.setTooltip(new Tooltip("not plotted in v1"));
                } else {
                    checkBox.setTooltip(null);
                }
                setGraphic(checkBox);
            }
        });

        // --- Name column -----------------------------------------------------
        TableColumn<DataChannel, String> nameCol = new TableColumn<>("Name");
        nameCol.setPrefWidth(200);
        nameCol.setCellValueFactory(cd ->
                new ReadOnlyStringWrapper(cd.getValue().name()));

        // --- DataType column -------------------------------------------------
        TableColumn<DataChannel, String> typeCol = new TableColumn<>("DataType");
        typeCol.setPrefWidth(90);
        typeCol.setCellValueFactory(cd ->
                new ReadOnlyStringWrapper(cd.getValue().dataType().wireName()));

        // --- Mode (Kind) column ----------------------------------------------
        TableColumn<DataChannel, String> modeCol = new TableColumn<>("Mode");
        modeCol.setPrefWidth(100);
        modeCol.setCellValueFactory(cd ->
                new ReadOnlyStringWrapper(cd.getValue().kind().name().toLowerCase()));

        // --- Samples column --------------------------------------------------
        TableColumn<DataChannel, Number> samplesCol = new TableColumn<>("Samples");
        samplesCol.setPrefWidth(80);
        samplesCol.setCellValueFactory(cd ->
                new SimpleObjectProperty<>(cd.getValue().sampleCount()));

        // --- Unit column -----------------------------------------------------
        TableColumn<DataChannel, String> unitCol = new TableColumn<>("Unit");
        unitCol.setPrefWidth(80);
        unitCol.setCellValueFactory(cd -> {
            String unit = cd.getValue().physicalUnit();
            return new ReadOnlyStringWrapper(unit != null ? unit : "");
        });

        getColumns().addAll(selectCol, nameCol, typeCol, modeCol, samplesCol, unitCol);
    }

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    /**
     * Get (or lazily create) the per-channel checkbox property, initialised to
     * {@code false} (unselected).
     */
    private SimpleBooleanProperty checkboxProperty(DataChannel ch) {
        return checkboxStates.computeIfAbsent(ch.name(), n -> new SimpleBooleanProperty(false));
    }

    /**
     * A channel is chartable when its DataType supports numeric double values.
     * Mirrors {@link ViewerModel#isChartable(DataChannel)}.
     */
    private static boolean isChartable(DataChannel ch) {
        DataType dt = ch.dataType();
        return switch (dt) {
            case BOOL,
                 INT8, INT16, INT32, INT64,
                 UINT8, UINT16, UINT32, UINT64,
                 FLOAT, DOUBLE -> true;
            default -> false;
        };
    }
}
