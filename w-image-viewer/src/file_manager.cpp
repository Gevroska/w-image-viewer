#include "pch.h"
#include "file_manager.h"
#include "config.h"
#include "include\helpers.h"
#include "include\supported_extensions.h"
#include "include\global.h"
#include "window.h"
#include "include\ensure.h"

bool File_manager::file_open(const wchar_t* path)
{
    return file_open(path, {g_config.image_apply_metadata_orientation.val, g_config.raw_thumb.val});
}

bool File_manager::file_open(const wchar_t* path, Decode_options options)
{
    try {
        std::error_code error;
        auto absolute = std::filesystem::absolute(path, error).lexically_normal();
        if (error) { last_error = error.message(); return false; }
        auto candidate = std::make_unique<Image>();
        if (!candidate->open(absolute, options)) { last_error = candidate->error; return false; }
        last_error.clear();
        pending_ = std::move(candidate);
        pending_path_ = std::move(absolute);
        return true;
    } catch (const std::exception& exception) {
        last_error = exception.what();
        return false;
    }
}

void File_manager::commit_open()
{
    if (!pending_) return;
    auto current = pending_path_;
    const auto files = collect_files(current);
    // Prefer an exact entry in directories that opt into case-sensitive names.
    if (std::find(files.begin(), files.end(), current) == files.end()) {
        for (const auto& file : files) {
            if (CompareStringOrdinal(file.c_str(), -1, current.c_str(), -1, TRUE) == CSTR_EQUAL) {
                current = file;
                break;
            }
        }
    }
    bool previous = false, next = false, alternative = false;
    for (const auto& file : files) {
        previous |= file < current;
        next |= file > current;
        alternative |= file != current;
    }
    // Directory enumeration and path allocation must finish before publishing.
    image = std::move(*pending_);
    file_current = std::move(current);
    pending_.reset();
    pending_path_.clear();
    has_previous_ = previous;
    has_next_ = next;
    has_alternative_ = alternative;
}

std::vector<std::filesystem::path> File_manager::collect_files(const std::filesystem::path& current) const
{
    std::vector<std::filesystem::path> files;
    if (current.empty()) return files;
    std::error_code error;
    std::filesystem::directory_iterator iterator(current.parent_path(), error), end;
    while (!error && iterator != end) {
        const auto& file = *iterator;
        if (file.is_regular_file(error)
            && PathMatchSpecExW(file.path().c_str(), WIV_SUPPORTED_EXTENSIONS, PMSF_MULTIPLE) == S_OK)
            files.push_back(file.path());
        iterator.increment(error);
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool File_manager::can_navigate(int direction) const
{
    return image.is_valid() && (g_config.cycle_files.val ? has_alternative_ : direction < 0 ? has_previous_ : has_next_);
}

bool File_manager::navigate(int direction)
{
    const auto files = collect_files(file_current);
    if (files.empty()) return false;
    const auto forward = std::upper_bound(files.begin(), files.end(), file_current);
    const auto backward = std::lower_bound(files.begin(), files.end(), file_current);
    const auto size = static_cast<std::ptrdiff_t>(files.size());
    auto index = direction > 0 ? forward - files.begin() : backward - files.begin() - 1;
    for (std::ptrdiff_t attempt = 0; attempt < size; ++attempt, index += direction) {
        if (index < 0 || index >= size) {
            if (!g_config.cycle_files.val) break;
            index = (index + size) % size;
        }
        if (files[index] != file_current && file_open(files[index].c_str())) return true;
    }
    return false;
}

bool File_manager::file_next() { return navigate(1); }
bool File_manager::file_previous() { return navigate(-1); }

bool File_manager::drag_and_drop(HDROP hdrop)
{
    const UINT length = DragQueryFileW(hdrop, 0, nullptr, 0);
    std::wstring path(length + 1, L'\0');
    DragQueryFileW(hdrop, 0, path.data(), length + 1);
    DragFinish(hdrop);
    path.resize(length);
    return length && file_open(path.c_str());
}

// Sends file to the recycle bin!
void File_manager::delete_file()
{
    if (!image.is_valid())
        return;

    // SHFILEOPSTRUCTW::pFrom must be double null terminated.
    std::wstring path = file_current.wstring();
    path.push_back(L'\0');

    // Configured to send file to the recycle bin.
    SHFILEOPSTRUCTW fileopenstruct = {};
    fileopenstruct.wFunc = FO_DELETE;
    fileopenstruct.pFrom = path.c_str();
    fileopenstruct.fFlags = FOF_ALLOWUNDO;

    if (SHFileOperationW(&fileopenstruct) == 0 && !fileopenstruct.fAnyOperationsAborted) {
        const bool opened = file_next() || file_previous();
        if (!opened) {
            image.close();
            file_current.clear();
            SendMessageW(g_hwnd, WIV_WM_RESET_RESOURCES, 0, 0);
        }
        else
            SendMessageW(g_hwnd, WIV_WM_OPEN_FILE, static_cast<WPARAM>(wiv::Open_reason::Navigation), 0);
    }
}
