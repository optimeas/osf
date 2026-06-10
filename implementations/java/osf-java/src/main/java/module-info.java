// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
module com.optimeas.osf {
    requires com.fasterxml.jackson.databind;
    requires org.slf4j;
    requires java.xml; // StAX for OSF4 XML metablock

    exports com.optimeas.osf;
    // com.optimeas.osf.internal is intentionally NOT exported.
}
