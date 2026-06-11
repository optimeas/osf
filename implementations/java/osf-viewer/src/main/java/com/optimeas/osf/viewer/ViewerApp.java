// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf.viewer;

import javafx.application.Application;
import javafx.scene.Scene;
import javafx.stage.Stage;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;

/**
 * JavaFX Application entry point for the OSF Viewer.
 *
 * <p>Presents a 1000x700 window with the full {@link MainView} layout
 * (toolbar + channel list + plot canvas + status bar). If a file path is
 * passed as the first launch argument it is opened automatically on startup.
 *
 * <p>Launch via the Maven javafx plugin:
 * <pre>
 *   mvn -o -pl osf-viewer -f implementations/java/pom.xml javafx:run
 * </pre>
 * or with an argument:
 * <pre>
 *   mvn ... javafx:run -Djavafx.args="path/to/file.osf"
 * </pre>
 */
public final class ViewerApp extends Application {

    @Override
    public void start(Stage stage) {
        MainView mainView = new MainView();

        stage.setTitle("OSF Viewer");
        stage.setScene(new Scene(mainView, 1000, 700));
        stage.show();

        // Auto-open a file if one was provided as a launch argument.
        List<String> args = getParameters().getRaw();
        if (!args.isEmpty()) {
            Path path = Paths.get(args.get(0));
            mainView.openFile(path);
        }
    }

    public static void main(String[] args) {
        launch(args);
    }
}
