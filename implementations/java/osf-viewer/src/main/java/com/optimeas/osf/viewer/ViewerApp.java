// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import javafx.application.Application;
import javafx.scene.Scene;
import javafx.scene.layout.BorderPane;
import javafx.stage.Stage;

public final class ViewerApp extends Application {
    @Override public void start(Stage stage) {
        stage.setTitle("OSF Viewer");
        stage.setScene(new Scene(new BorderPane(), 1000, 700));
        stage.show();
    }
    public static void main(String[] args) { launch(args); }
}
