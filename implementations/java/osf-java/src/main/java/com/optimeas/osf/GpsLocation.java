// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH
package com.optimeas.osf;

/** A GPS sample: latitude, longitude (degrees) and altitude (metres). */
public record GpsLocation(double latitude, double longitude, double altitude) {}
