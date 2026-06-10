// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

/** Base type for all OSF library errors. */
public class OsfException extends RuntimeException {
    public OsfException(String message) { super(message); }
    public OsfException(String message, Throwable cause) { super(message, cause); }

    /** A data type or channel type that the spec removed, or an unknown type. */
    public static final class UnsupportedType extends OsfException {
        public UnsupportedType(String message) { super(message); }
    }

    /** Malformed magic header / metablock / block structure. */
    public static final class MalformedFile extends OsfException {
        public MalformedFile(String message) { super(message); }
        public MalformedFile(String message, Throwable cause) { super(message, cause); }
    }
}
