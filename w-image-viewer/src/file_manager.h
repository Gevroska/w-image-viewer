#pragma once

#include "pch.h"
#include "image.h"

class File_manager
{
public:
    bool file_open(const wchar_t* path);
    bool file_open(const wchar_t* path, Decode_options options);
    bool file_next();
    bool file_previous();
    bool can_navigate(int direction) const;
    const Image* pending_image() const noexcept { return pending_.get(); }
    void commit_open();
    void cancel_open() noexcept { pending_.reset(); }
    bool drag_and_drop(HDROP hdrop);
    void delete_file();
    std::filesystem::path file_current;
    Image image;
    std::string last_error;
private:
    bool navigate(int direction);
    std::vector<std::filesystem::path> collect_files(const std::filesystem::path& current) const;
    std::unique_ptr<Image> pending_;
    std::filesystem::path pending_path_;
    bool has_previous_ = false, has_next_ = false, has_alternative_ = false;
};
