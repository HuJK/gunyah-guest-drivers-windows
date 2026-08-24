// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "ViosndEndpoint.h"

/*
 * Naming an endpoint so that a card carrying several of them shows several names.
 *
 * Windows composes what the user sees from the endpoint's own description and the device's --
 * "Speakers (XCB VirtIO Audio Device)" -- and the first half comes from a GUID on the topology
 * filter's bridge pin, resolved through HKLM\SYSTEM\CurrentControlSet\Control\MediaCategories.
 * With every endpoint carrying KSNODETYPE_SPEAKER, every endpoint is called "Speakers".
 *
 * So each endpoint gets a GUID of its own and a string written against it. The GUIDs are fixed
 * and built in rather than derived from the host device: Windows treats a new GUID as a new
 * endpoint, and a new endpoint loses the volume and the default-device choice the user set on
 * the old one. A name that follows the host device would therefore cost the user their settings
 * every time they changed headphones.
 *
 * Measured, and the reason the write has to happen before the subdevice is registered: the
 * lookup runs once, when Windows first builds the endpoint, and the answer is cached in the
 * endpoint's own key. Changing the string afterwards does nothing.
 */

/* The name GUID for one endpoint slot, or NULL when the slot is past the pool. */
const GUID *ViosndEndpointNameGuid(_In_ BOOLEAN Capture, _In_ ULONG Index);

/*
 * Publishes the string for that slot, derived from what the host said the endpoint is. Must be
 * called before PcRegisterSubdevice. Failure is not fatal: the endpoint then falls back to being
 * named after its node type, which is what it was called before any of this existed.
 */
NTSTATUS ViosndPublishEndpointName(_In_ BOOLEAN Capture, _In_ ULONG Index, _In_ ULONG Kind);
