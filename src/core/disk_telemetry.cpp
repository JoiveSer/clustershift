#include "core/disk_telemetry.hpp"
#include <winioctl.h>
#include <ntddstor.h>
#include <vector>

namespace clustershift::core {

const wchar_t* DiskTelemetry::bus_type_to_string(BusTypeKind bus) noexcept {
    switch (bus) {
        case BusTypeKind::Nvme:    return L"NVMe (PCIe Express)";
        case BusTypeKind::Sata:    return L"Serial ATA (SATA)";
        case BusTypeKind::Usb:     return L"USB Removable";
        case BusTypeKind::Sas:     return L"Serial Attached SCSI (SAS)";
        case BusTypeKind::Scsi:    return L"SCSI";
        case BusTypeKind::Ata:     return L"Parallel ATA (IDE)";
        case BusTypeKind::Virtual: return L"Virtual / VHD";
        default:                   return L"Standard Storage Bus";
    }
}

const char* DiskTelemetry::bus_type_to_string_a(BusTypeKind bus) noexcept {
    switch (bus) {
        case BusTypeKind::Nvme:    return "NVMe (PCIe Express)";
        case BusTypeKind::Sata:    return "Serial ATA (SATA)";
        case BusTypeKind::Usb:     return "USB Removable";
        case BusTypeKind::Sas:     return "Serial Attached SCSI (SAS)";
        case BusTypeKind::Scsi:    return "SCSI";
        case BusTypeKind::Ata:     return "Parallel ATA (IDE)";
        case BusTypeKind::Virtual: return "Virtual / VHD";
        default:                   return "Standard Storage Bus";
    }
}

namespace {
BusTypeKind map_bus_type(STORAGE_BUS_TYPE type) noexcept {
    switch (type) {
        case BusTypeScsi:       return BusTypeKind::Scsi;
        case BusTypeAtapi:      return BusTypeKind::Atapi;
        case BusTypeAta:        return BusTypeKind::Ata;
        case BusTypeSata:       return BusTypeKind::Sata;
        case BusTypeSas:        return BusTypeKind::Sas;
        case BusTypeUsb:        return BusTypeKind::Usb;
        case BusTypeNvme:       return BusTypeKind::Nvme;
        case BusTypeVirtual:    return BusTypeKind::Virtual;
        default:                return BusTypeKind::Unknown;
    }
}

std::wstring extract_ansi_string(const uint8_t* base, DWORD offset) {
    if (offset == 0) return L"";
    const char* str = reinterpret_cast<const char*>(base + offset);
    if (!str || !*str) return L"";
    
    std::wstring result;
    while (*str) {
        if (*str != ' ' || (!result.empty() && result.back() != L' ')) {
            result.push_back(static_cast<wchar_t>(*str));
        }
        ++str;
    }
    while (!result.empty() && result.back() == L' ') {
        result.pop_back();
    }
    return result;
}
} // namespace

std::expected<DiskHealthTelemetry, DWORD> DiskTelemetry::query_telemetry(HANDLE volume_handle) {
    DiskHealthTelemetry telemetry{};

    // 1. Query Device Descriptor for Model, Vendor and Bus Type
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    std::vector<uint8_t> buffer(4096);
    DWORD bytes_returned = 0;

    if (DeviceIoControl(
            volume_handle,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &query,
            sizeof(query),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &bytes_returned,
            nullptr)) {
        const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
        telemetry.bus_type = map_bus_type(desc->BusType);
        telemetry.vendor_id = extract_ansi_string(buffer.data(), desc->VendorIdOffset);
        telemetry.product_id = extract_ansi_string(buffer.data(), desc->ProductIdOffset);
        telemetry.serial_number = extract_ansi_string(buffer.data(), desc->SerialNumberOffset);
    }

    // 2. Query Failure Prediction (S.M.A.R.T.)
    STORAGE_PREDICT_FAILURE predict{};
    if (DeviceIoControl(
            volume_handle,
            IOCTL_STORAGE_PREDICT_FAILURE,
            nullptr,
            0,
            &predict,
            sizeof(predict),
            &bytes_returned,
            nullptr)) {
        telemetry.is_failure_predicted = (predict.PredictFailure != 0);
    }

    // 3. Query Temperature (Windows 10 1709+ / Windows 11)
    STORAGE_PROPERTY_QUERY temp_query{};
    temp_query.PropertyId = StorageDeviceTemperatureProperty;
    temp_query.QueryType = PropertyStandardQuery;

    std::vector<uint8_t> temp_buf(1024);
    if (DeviceIoControl(
            volume_handle,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &temp_query,
            sizeof(temp_query),
            temp_buf.data(),
            static_cast<DWORD>(temp_buf.size()),
            &bytes_returned,
            nullptr)) {
        const auto* temp_desc = reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(temp_buf.data());
        if (temp_desc->InfoCount > 0 && temp_desc->TemperatureInfo[0].Temperature > 0) {
            telemetry.temperature_celsius = static_cast<int32_t>(temp_desc->TemperatureInfo[0].Temperature);
        }
    }

    // 4. Query DirectStorage / BypassIO Telemetry (Windows 11)
#ifdef FSCTL_MANAGE_BYPASS_IO
    FS_BPIO_INPUT bpio_in{};
    bpio_in.Operation = FS_BPIO_OP_QUERY;
    bpio_in.InFlags = FSBPIO_INFL_None;

    FS_BPIO_OUTPUT bpio_out{};
    if (DeviceIoControl(
            volume_handle,
            FSCTL_MANAGE_BYPASS_IO,
            &bpio_in,
            sizeof(bpio_in),
            &bpio_out,
            sizeof(bpio_out),
            &bytes_returned,
            nullptr)) {
        telemetry.bypass_io.is_supported = (bpio_out.Query.OpStatus == 0);
        telemetry.bypass_io.status_code = bpio_out.Query.OpStatus;
        telemetry.bypass_io.is_filter_blocked = (bpio_out.OutFlags & FSBPIO_OUTFL_FILTER_ATTACH_BLOCKED) != 0;
        telemetry.bypass_io.is_storage_driver_compatible = (bpio_out.OutFlags & FSBPIO_OUTFL_COMPATIBLE_STORAGE_DRIVER) != 0;

        if (bpio_out.Query.FailingDriverNameLen > 0) {
            size_t len = (std::min)(static_cast<size_t>(bpio_out.Query.FailingDriverNameLen), size_t(32));
            telemetry.bypass_io.failing_driver = std::wstring(bpio_out.Query.FailingDriverName, len);
        }
        if (bpio_out.Query.FailureReasonLen > 0) {
            size_t len = (std::min)(static_cast<size_t>(bpio_out.Query.FailureReasonLen), size_t(128));
            telemetry.bypass_io.failure_reason = std::wstring(bpio_out.Query.FailureReason, len);
        }
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_INVALID_FUNCTION || err == ERROR_NOT_SUPPORTED) {
            telemetry.bypass_io.failure_reason = L"BypassIO not supported on this Windows kernel (Windows 11 required)";
        } else {
            telemetry.bypass_io.status_code = err;
        }
    }
#endif

    return telemetry;
}

} // namespace clustershift::core
