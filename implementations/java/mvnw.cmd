@REM Maven wrapper batch script — generated for osf-java
@REM Licensed to the Apache Software Foundation (ASF) under one
@REM or more contributor license agreements.  See the NOTICE file

@IF "%__MVNW_ARG0_NAME__%"=="" (SET "BASE_DIR=%~dp0") ELSE (SET "BASE_DIR=%__MVNW_ARG0_NAME__%")
@SET WRAPPER_JAR=%BASE_DIR%.mvn\wrapper\maven-wrapper.jar
@SET WRAPPER_LAUNCHER=org.apache.maven.wrapper.MavenWrapperMain
@SET MAVEN_PROJECTBASEDIR=%BASE_DIR%

@IF NOT EXIST "%WRAPPER_JAR%" (
  @echo Downloading Maven Wrapper...
  @IF EXIST "%JAVA_HOME%\bin\java.exe" (
    "%JAVA_HOME%\bin\java.exe" -cp . org.apache.maven.wrapper.BootstrapMainStarter "%WRAPPER_JAR%" %*
  )
)

@"%JAVA_HOME%\bin\java.exe" ^
  -classpath "%WRAPPER_JAR%" ^
  "-Dmaven.multiModuleProjectDirectory=%MAVEN_PROJECTBASEDIR%" ^
  %WRAPPER_LAUNCHER% %*
@SET MAVEN_CMDLINE_ARGS=%*
