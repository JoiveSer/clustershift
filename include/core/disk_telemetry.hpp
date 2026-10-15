#pragma once

#include "scoped_handle.hpp"
#include <windows.h>
#include <string>
#include <cstdint>
#include <expected>

namespace clustershift::core {

enum class BusTypeKind {
    Unknown,
    Scsi,
    Atapi,
    Ata,
    Sata,
    Sas,
    Usb,
    Nvme,
    Virtual
};

struct DiskHealthTelemetry {
    std::wstring vendor_id;
    std::wstring product_id;
    std::wstring serial_number;
    BusTypeKind bus_type{BusTypeKind::Unknown};
    bool is_failure_predicted{false};
    int32_t temperature_celsius{-1};
};

class DiskTelemetry {
public:
    static std::expected<DiskHealthTelemetry, DWORD> query_telemetry(HANDLE volume_handle);
    static const wchar_t* bus_type_to_string(BusTypeKind bus) noexcept;
};

} // namespace clustershift::core
