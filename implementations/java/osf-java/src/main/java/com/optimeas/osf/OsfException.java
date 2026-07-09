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

    /**
     * A magic-header token whose key is not understood (must-understand rule).
     * Distinct from {@link MalformedFile} so callers can tell an unknown
     * integrity/extension token apart from a structurally broken header, and so
     * the diagnostic never surfaces as a misleading number-format error.
     */
    public static final class UnknownHeaderToken extends OsfException {
        public UnknownHeaderToken(String message) { super(message); }
    }

    /**
     * The metablock CRC32C carried by the {@code crc32c} header token does not
     * match the raw metablock bytes — the file is rejected under an active
     * integrity profile.
     */
    public static final class MetablockCrcMismatch extends OsfException {
        public MetablockCrcMismatch(String message) { super(message); }
    }
}
