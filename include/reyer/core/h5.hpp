#pragma once

#include <cstring>
#include <hdf5.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace reyer::h5 {

// Error checking helpers
namespace detail {

inline hid_t check_id(hid_t id, const char *func) {
    if (id < 0) {
        throw std::runtime_error(std::string(func) + " failed");
    }
    return id;
}

inline herr_t check_err(herr_t err, const char *func) {
    if (err < 0) {
        throw std::runtime_error(std::string(func) + " failed");
    }
    return err;
}

} // namespace detail

// RAII wrapper for owned HDF5 type IDs
class TypeId {
  public:
    explicit TypeId(hid_t id) : id_(id) {}

    ~TypeId() {
        if (id_ >= 0)
            H5Tclose(id_);
    }

    TypeId(const TypeId &) = delete;
    TypeId &operator=(const TypeId &) = delete;

    TypeId(TypeId &&other) noexcept : id_(other.id_) { other.id_ = -1; }

    TypeId &operator=(TypeId &&other) noexcept {
        if (this != &other) {
            if (id_ >= 0)
                H5Tclose(id_);
            id_ = other.id_;
            other.id_ = -1;
        }
        return *this;
    }

    hid_t get() const { return id_; }

    hid_t release() {
        hid_t temp = id_;
        id_ = -1;
        return temp;
    }

  private:
    hid_t id_;
};

// Helper trait to get HDF5 type from C++ type.
// All specializations return OWNED type IDs — callers must H5Tclose() the
// result.
template <typename T> struct hdf5_type_traits;

#define H5_NATIVE_TYPE_TRAIT(CppType, H5Type)                                  \
    template <> struct hdf5_type_traits<CppType> {                             \
        static hid_t get() {                                                   \
            return detail::check_id(H5Tcopy(H5Type), "H5Tcopy");               \
        }                                                                      \
    };

H5_NATIVE_TYPE_TRAIT(char, H5T_NATIVE_CHAR)
H5_NATIVE_TYPE_TRAIT(unsigned char, H5T_NATIVE_UCHAR)
H5_NATIVE_TYPE_TRAIT(short, H5T_NATIVE_SHORT)
H5_NATIVE_TYPE_TRAIT(unsigned short, H5T_NATIVE_USHORT)
H5_NATIVE_TYPE_TRAIT(int, H5T_NATIVE_INT)
H5_NATIVE_TYPE_TRAIT(unsigned int, H5T_NATIVE_UINT)
H5_NATIVE_TYPE_TRAIT(long, H5T_NATIVE_LONG)
H5_NATIVE_TYPE_TRAIT(unsigned long, H5T_NATIVE_ULONG)
H5_NATIVE_TYPE_TRAIT(long long, H5T_NATIVE_LLONG)
H5_NATIVE_TYPE_TRAIT(unsigned long long, H5T_NATIVE_ULLONG)
H5_NATIVE_TYPE_TRAIT(float, H5T_NATIVE_FLOAT)
H5_NATIVE_TYPE_TRAIT(double, H5T_NATIVE_DOUBLE)

#undef H5_NATIVE_TYPE_TRAIT

// bool — map to uint8 since sizeof(bool) == 1 on all common platforms
template <> struct hdf5_type_traits<bool> {
    static_assert(sizeof(bool) == 1, "bool must be 1 byte for HDF5 mapping");
    static hid_t get() {
        return detail::check_id(H5Tcopy(H5T_NATIVE_UINT8), "H5Tcopy");
    }
};

// Fixed-length strings
template <std::size_t N> struct hdf5_type_traits<char[N]> {
    static hid_t get() {
        hid_t str_type = detail::check_id(H5Tcopy(H5T_C_S1), "H5Tcopy");
        detail::check_err(H5Tset_size(str_type, N), "H5Tset_size");
        return str_type;
    }
};

// Variable-length strings
template <> struct hdf5_type_traits<std::string> {
    static hid_t get() {
        hid_t str_type = detail::check_id(H5Tcopy(H5T_C_S1), "H5Tcopy");
        detail::check_err(H5Tset_size(str_type, H5T_VARIABLE), "H5Tset_size");
        return str_type;
    }
};

// Insert a member into a compound type, managing the member type lifetime
template <typename MemberType>
void add_member(hid_t compound, const char *name, std::size_t offset) {
    hid_t member_type = hdf5_type_traits<MemberType>::get();
    herr_t err = H5Tinsert(compound, name, offset, member_type);
    H5Tclose(member_type);
    detail::check_err(err, "H5Tinsert");
}

class File {
  public:
    File(const std::string &filename, unsigned flags = H5F_ACC_TRUNC) {
        file_ = detail::check_id(
            H5Fcreate(filename.c_str(), flags, H5P_DEFAULT, H5P_DEFAULT),
            "H5Fcreate");
    }

    ~File() {
        if (file_ >= 0)
            H5Fclose(file_);
    }

    hid_t get() const { return file_; }

  private:
    hid_t file_;
};

template <typename T> class Dataset {
  public:
    explicit Dataset(hid_t parent_id, const std::string &name,
                     size_t chunk_size = 1024)
        : size_(0), buffer_capacity_(chunk_size) {
        type_id_ = hdf5_type_traits<T>::get();
        buffer_.reserve(buffer_capacity_);

        hsize_t max_dims[1] = {H5S_UNLIMITED};
        hsize_t initial_dims[1] = {0};
        hid_t space = H5Screate_simple(1, initial_dims, max_dims);

        hid_t props = H5Pcreate(H5P_DATASET_CREATE);
        hsize_t chunks[1] = {chunk_size};
        H5Pset_chunk(props, 1, chunks);

        dataset_ = H5Dcreate2(parent_id, name.c_str(), type_id_, space,
                              H5P_DEFAULT, props, H5P_DEFAULT);

        H5Pclose(props);
        H5Sclose(space);
    }

    ~Dataset() {
        flush();
        H5Dclose(dataset_);
        H5Tclose(type_id_);
    }

    void write(const T &value) {
        buffer_.push_back(value);
        if (buffer_.size() >= buffer_capacity_) {
            flush();
        }
    }

    void write(const T *data, size_t count) { write_direct(data, count); }

    template <typename Container> void write(const Container &values) {
        write_direct(std::data(values), std::size(values));
    }

    void flush() {
        if (buffer_.empty())
            return;
        write_direct(buffer_.data(), buffer_.size());
        buffer_.clear();
    }

    template <typename A> void set_attr(const char *name, const A &value) {
        hid_t attr_type = hdf5_type_traits<A>::get();
        hid_t space = H5Screate(H5S_SCALAR);
        hid_t attr = H5Acreate2(dataset_, name, attr_type, space, H5P_DEFAULT,
                                H5P_DEFAULT);
        H5Awrite(attr, attr_type, &value);
        H5Aclose(attr);
        H5Sclose(space);
        H5Tclose(attr_type);
    }

    void set_attr(const char *name, const char *value) {
        hid_t attr_type = H5Tcopy(H5T_C_S1);
        H5Tset_size(attr_type, strlen(value) + 1);
        hid_t space = H5Screate(H5S_SCALAR);
        hid_t attr = H5Acreate2(dataset_, name, attr_type, space, H5P_DEFAULT,
                                H5P_DEFAULT);
        H5Awrite(attr, attr_type, value);
        H5Aclose(attr);
        H5Sclose(space);
        H5Tclose(attr_type);
    }

  private:
    void write_direct(const T *data, size_t count) {
        hsize_t new_size[1] = {size_ + count};
        H5Dset_extent(dataset_, new_size);

        hid_t space = H5Dget_space(dataset_);
        hsize_t offset[1] = {size_};
        hsize_t hcount[1] = {count};
        H5Sselect_hyperslab(space, H5S_SELECT_SET, offset, nullptr, hcount,
                            nullptr);

        hid_t memspace = H5Screate_simple(1, hcount, nullptr);

        H5Dwrite(dataset_, type_id_, memspace, space, H5P_DEFAULT, data);

        H5Sclose(memspace);
        H5Sclose(space);

        size_ += count;
    }

    hid_t dataset_;
    hid_t type_id_;
    hsize_t size_;
    size_t buffer_capacity_;
    std::vector<T> buffer_;
};

class Group {
  public:
    Group(hid_t parent, const std::string &name) {
        group_ = detail::check_id(
            H5Gcreate2(parent, name.c_str(), H5P_DEFAULT, H5P_DEFAULT,
                       H5P_DEFAULT),
            "H5Gcreate2");
    }

    ~Group() {
        if (group_ >= 0)
            H5Gclose(group_);
    }

    Group(const Group &) = delete;
    Group &operator=(const Group &) = delete;
    Group(Group &&other) noexcept : group_(other.group_) { other.group_ = -1; }
    Group &operator=(Group &&other) noexcept {
        if (this != &other) {
            if (group_ >= 0)
                H5Gclose(group_);
            group_ = other.group_;
            other.group_ = -1;
        }
        return *this;
    }

    hid_t get() const { return group_; }

  private:
    hid_t group_;
};

} // namespace reyer::h5

#define H5_AUTO_FIELD(field_name)                                              \
    reyer::h5::add_member<decltype(Type::field_name)>(                        \
        type_id.get(), #field_name, offsetof(Type, field_name));

#define H5_NAMED_FIELD(field_name, h5_name)                                    \
    reyer::h5::add_member<decltype(Type::field_name)>(                        \
        type_id.get(), h5_name, offsetof(Type, field_name));

#define H5_DEFINE_TYPE(TypeArg, ...)                                           \
    namespace reyer::h5 {                                                      \
    template <> struct hdf5_type_traits<TypeArg> {                             \
        static hid_t get() {                                                   \
            using Type = TypeArg;                                              \
            TypeId type_id(detail::check_id(                                   \
                H5Tcreate(H5T_COMPOUND, sizeof(Type)), "H5Tcreate"));          \
            __VA_ARGS__                                                        \
            return type_id.release();                                          \
        }                                                                      \
    };                                                                         \
    }

#include "core.hpp"

H5_DEFINE_TYPE(reyer::vec2<float>,
    H5_AUTO_FIELD(x)
    H5_AUTO_FIELD(y)
)

H5_DEFINE_TYPE(reyer::core::DpiData,
    H5_AUTO_FIELD(p1)
    H5_AUTO_FIELD(p4)
    H5_AUTO_FIELD(pupil_center)
    H5_AUTO_FIELD(pupil_diameter)
)

H5_DEFINE_TYPE(reyer::core::GazeData,
    H5_AUTO_FIELD(raw)
    H5_AUTO_FIELD(filtered)
    H5_AUTO_FIELD(velocity)
)

H5_DEFINE_TYPE(reyer::core::TrackerData,
    H5_AUTO_FIELD(dpi)
    H5_AUTO_FIELD(gaze)
    H5_AUTO_FIELD(is_blink)
    H5_AUTO_FIELD(is_valid)
)

H5_DEFINE_TYPE(reyer::core::EyeData,
    H5_AUTO_FIELD(left)
    H5_AUTO_FIELD(right)
    H5_AUTO_FIELD(timestamp)
)
