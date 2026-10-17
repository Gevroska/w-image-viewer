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
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error).lexically_normal();
    if (error) return false;
    auto candidate = std::make_unique<Image>();
    if (!candidate->open(absolute)) return false;
    pending_ = std::move(candidate);
    pending_path_ = std::move(absolute);
    return true;
}

void File_manager::commit_open()
{
    if (!pending_) return;
    image = std::move(*pending_);
    pending_.reset();
    file_current = std::move(pending_path_);
    has_previous_ = has_next_ = has_alternative_ = false;
    const auto files = collect_files();
    for (const auto& file : files) {
        std::error_code error;
        if (std::filesystem::equivalent(file, file_current, error)) { file_current = file; break; }
    }
    for (const auto& file : files) {
        has_previous_ |= file < file_current;
        has_next_ |= file > file_current;
        has_alternative_ |= file != file_current;
    }
}

std::vector<std::filesystem::path> File_manager::collect_files() const
{
    std::vector<std::filesystem::path> files;
    if (file_current.empty()) return files;
    std::error_code error;
    std::filesystem::directory_iterator iterator(file_current.parent_path(), error), end;
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
    const auto files = collect_files();
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
    wchar_t path[MAX_PATH + 1];
    wcscpy(path, file_current.c_str());
    path[std::char_traits<wchar_t>::length(path) + 1] = '\0';

    // Configured to send file to the recycle bin.
    SHFILEOPSTRUCTW fileopenstruct = {};
    fileopenstruct.wFunc = FO_DELETE;
    fileopenstruct.pFrom = path;
    fileopenstruct.fFlags = FOF_ALLOWUNDO;

    if (SHFileOperationW(&fileopenstruct) == 0) {
        const bool opened = file_next() || file_previous();
        if (!opened) {
            image.close();
            file_current.clear();
            ensure(PostMessageW(g_hwnd, WIV_WM_RESET_RESOURCES, 0, 0), != 0);
        }
        else
            ensure(PostMessageW(g_hwnd, WIV_WM_OPEN_FILE, 0, 0), != 0);
    }
}
