/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later

    A Qt-free port of KWin's src/utils/xcbutils, reduced to the pieces this
    project uses; whenever a new piece is needed it is copied over from
    upstream and adapted here. Adaptations and local additions are marked
    "local:" — everything else stays close enough to upstream that future
    pieces move by copy:
      - QByteArray / QString in wrapper APIs became std::string;
      - upstream's Rect (core/rect.h) became the local POD below;
      - upstream gets its connection from the compositor's app singleton,
        this port opens one lazily and keeps it for the process lifetime.
*/
#ifndef WALLPAPER_ENGINE_UTILS_XCBUTILS_H
#define WALLPAPER_ENGINE_UTILS_XCBUTILS_H

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <xcb/randr.h>
#include <xcb/xcb.h>

namespace KWin
{

namespace Xcb
{

// ---- local: connection ownership -------------------------------------------
// Upstream reaches the compositor's single X11 connection through the app
// singleton. Here the service layer opens one lazily and keeps it for the
// process lifetime; the wrappers below query through connection() and never
// manage connect/disconnect themselves. Single-threaded callers only.
xcb_connection_t *connection();
xcb_window_t rootWindow();

// local: upstream takes this constant from effect/xcb.h
constexpr xcb_window_t XCB_WINDOW_NONE = 0;

typedef xcb_window_t WindowId;

/**
 * @brief Variadic template to wrap an xcb request.
 *
 * This struct is part of the generic implementation to wrap xcb requests
 * and fetching their reply. Each request is represented by two templated
 * elements: WrapperData and Wrapper.
 *
 * The WrapperData defines the types of the xcb request: the reply_type, the
 * cookie_type, the function pointer types for the xcb request and reply
 * functions, over the variadic xcb request arguments.
 *
 * As the WrapperData does not specify the actual function pointers one needs
 * to derive another struct which specifies them, or use the XCB_WRAPPER_DATA
 * macro:
 * @code
 * XCB_WRAPPER_DATA(GeometryData, xcb_get_geometry, xcb_drawable_t)
 * @endcode
 *
 * The Wrapper provides an easy to use RAII API which calls the WrapperData's
 * requestFunc in the ctor and fetches the reply the first time it is used.
 * In addition the dtor takes care of freeing the reply if it got fetched,
 * otherwise it discards the reply. The Wrapper can be used as if it were the
 * reply_type directly, and casts to bool as "the reply arrived".
 */
template<typename Reply,
         typename Cookie,
         typename... Args>
struct WrapperData
{
    typedef Reply reply_type;
    typedef Cookie cookie_type;
    typedef std::tuple<Args...> argument_types;
    typedef Cookie (*request_func)(xcb_connection_t *, Args...);
    typedef Reply *(*reply_func)(xcb_connection_t *, Cookie, xcb_generic_error_t **);
    static constexpr std::size_t argumentCount = sizeof...(Args);
};

/**
 * @brief Partial template specialization for WrapperData with no further arguments.
 */
template<typename Reply,
         typename Cookie>
struct WrapperData<Reply, Cookie>
{
    typedef Reply reply_type;
    typedef Cookie cookie_type;
    typedef std::tuple<> argument_types;
    typedef Cookie (*request_func)(xcb_connection_t *);
    typedef Reply *(*reply_func)(xcb_connection_t *, Cookie, xcb_generic_error_t **);
    static constexpr std::size_t argumentCount = 0;
};

/**
 * @brief Abstract base class for the wrapper.
 *
 * This class contains the complete functionality of the Wrapper. It's only an abstract
 * base class to provide partial template specialization for more specific constructors.
 */
template<typename Data>
class AbstractWrapper
{
public:
    typedef typename Data::cookie_type Cookie;
    typedef typename Data::reply_type Reply;
    virtual ~AbstractWrapper()
    {
        cleanup();
    }
    inline AbstractWrapper &operator=(const AbstractWrapper &other)
    {
        if (this != &other) {
            // if we had managed a reply, free it
            cleanup();
            // copy members
            m_retrieved = other.m_retrieved;
            m_cookie = other.m_cookie;
            m_window = other.m_window;
            m_reply = other.m_reply;
            // take over the responsibility for the reply pointer
            takeFromOther(const_cast<AbstractWrapper &>(other));
        }
        return *this;
    }

    inline const Reply *operator->()
    {
        getReply();
        return m_reply;
    }
    inline bool isNull()
    {
        getReply();
        return m_reply == nullptr;
    }
    inline bool isNull() const
    {
        const_cast<AbstractWrapper *>(this)->getReply();
        return m_reply == nullptr;
    }
    inline operator bool()
    {
        return !isNull();
    }
    inline operator bool() const
    {
        return !isNull();
    }
    inline const Reply *data()
    {
        getReply();
        return m_reply;
    }
    inline const Reply *data() const
    {
        const_cast<AbstractWrapper *>(this)->getReply();
        return m_reply;
    }
    inline WindowId window() const
    {
        return m_window;
    }
    inline bool isRetrieved() const
    {
        return m_retrieved;
    }
    /**
     * Returns the value of the reply pointer referenced by this object. The reply pointer of
     * this object will be reset to null. Calling any method which requires the reply to be valid
     * will crash.
     *
     * Callers of this function take ownership of the pointer.
     */
    inline Reply *take()
    {
        getReply();
        Reply *ret = m_reply;
        m_reply = nullptr;
        m_window = XCB_WINDOW_NONE;
        return ret;
    }

protected:
    AbstractWrapper()
        : m_retrieved(false)
        , m_window(XCB_WINDOW_NONE)
        , m_reply(nullptr)
    {
        m_cookie.sequence = 0;
    }
    explicit AbstractWrapper(WindowId window, Cookie cookie)
        : m_retrieved(false)
        , m_cookie(cookie)
        , m_window(window)
        , m_reply(nullptr)
    {
    }
    explicit AbstractWrapper(const AbstractWrapper &other)
        : m_retrieved(other.m_retrieved)
        , m_cookie(other.m_cookie)
        , m_window(other.m_window)
        , m_reply(nullptr)
    {
        takeFromOther(const_cast<AbstractWrapper &>(other));
    }
    void getReply()
    {
        if (m_retrieved || !m_cookie.sequence) {
            return;
        }
        m_reply = Data::replyFunc(connection(), m_cookie, nullptr);
        m_retrieved = true;
    }

private:
    inline void cleanup()
    {
        if (!m_retrieved && m_cookie.sequence) {
            xcb_discard_reply(connection(), m_cookie.sequence);
        } else if (m_reply) {
            free(m_reply);
        }
    }
    inline void takeFromOther(AbstractWrapper &other)
    {
        if (m_retrieved) {
            m_reply = other.take();
        } else {
            // ensure that other object doesn't try to get the reply or discards it in the dtor
            other.m_retrieved = true;
            other.m_window = XCB_WINDOW_NONE;
        }
    }
    bool m_retrieved;
    Cookie m_cookie;
    WindowId m_window;
    Reply *m_reply;
};

/**
 * @brief Template to compare the arguments of two std::tuple.
 *
 * @internal Used by static_assert in Wrapper
 */
template<typename T1, typename T2, std::size_t I>
struct tupleCompare
{
    typedef typename std::tuple_element<I, T1>::type tuple1Type;
    typedef typename std::tuple_element<I, T2>::type tuple2Type;
    static constexpr bool value = std::is_same<tuple1Type, tuple2Type>::value && tupleCompare<T1, T2, I - 1>::value;
};

/**
 * @brief Recursive template case for first tuple element.
 */
template<typename T1, typename T2>
struct tupleCompare<T1, T2, 0>
{
    typedef typename std::tuple_element<0, T1>::type tuple1Type;
    typedef typename std::tuple_element<0, T2>::type tuple2Type;
    static constexpr bool value = std::is_same<tuple1Type, tuple2Type>::value;
};

/**
 * @brief Wrapper taking a WrapperData as first template argument and xcb request args as variadic args.
 */
template<typename Data, typename... Args>
class Wrapper : public AbstractWrapper<Data>
{
public:
    static_assert(!std::is_same<Data, Xcb::WrapperData<typename Data::reply_type, typename Data::cookie_type, Args...>>::value,
                  "Data template argument must be derived from WrapperData");
    static_assert(std::is_base_of<Xcb::WrapperData<typename Data::reply_type, typename Data::cookie_type, Args...>, Data>::value,
                  "Data template argument must be derived from WrapperData");
    static_assert(sizeof...(Args) == Data::argumentCount,
                  "Wrapper and WrapperData need to have same template argument count");
    static_assert(tupleCompare<std::tuple<Args...>, typename Data::argument_types, sizeof...(Args) - 1>::value,
                  "Argument miss-match between Wrapper and WrapperData");
    Wrapper() = default;
    explicit Wrapper(Args... args)
        : AbstractWrapper<Data>(XCB_WINDOW_NONE, Data::requestFunc(connection(), args...))
    {
    }
    explicit Wrapper(xcb_window_t w, Args... args)
        : AbstractWrapper<Data>(w, Data::requestFunc(connection(), args...))
    {
    }
};

/**
 * @brief Template specialization for xcb_window_t being first variadic argument.
 */
template<typename Data, typename... Args>
class Wrapper<Data, xcb_window_t, Args...> : public AbstractWrapper<Data>
{
public:
    static_assert(!std::is_same<Data, Xcb::WrapperData<typename Data::reply_type, typename Data::cookie_type, xcb_window_t, Args...>>::value,
                  "Data template argument must be derived from WrapperData");
    static_assert(std::is_base_of<Xcb::WrapperData<typename Data::reply_type, typename Data::cookie_type, xcb_window_t, Args...>, Data>::value,
                  "Data template argument must be derived from WrapperData");
    static_assert(sizeof...(Args) + 1 == Data::argumentCount,
                  "Wrapper and WrapperData need to have same template argument count");
    static_assert(tupleCompare<std::tuple<xcb_window_t, Args...>, typename Data::argument_types, sizeof...(Args)>::value,
                  "Argument miss-match between Wrapper and WrapperData");
    Wrapper() = default;
    explicit Wrapper(xcb_window_t w, Args... args)
        : AbstractWrapper<Data>(w, Data::requestFunc(connection(), w, args...))
    {
    }
};

/**
 * @brief Template specialization for no variadic arguments.
 *
 * It's needed to prevent ambiguous constructors being generated.
 */
template<typename Data>
class Wrapper<Data> : public AbstractWrapper<Data>
{
public:
    static_assert(!std::is_same<Data, Xcb::WrapperData<typename Data::reply_type, typename Data::cookie_type>>::value,
                  "Data template argument must be derived from WrapperData");
    static_assert(std::is_base_of<Xcb::WrapperData<typename Data::reply_type, typename Data::cookie_type>, Data>::value,
                  "Data template argument must be derived from WrapperData");
    static_assert(Data::argumentCount == 0, "Wrapper for no arguments constructed with WrapperData with arguments");
    explicit Wrapper()
        : AbstractWrapper<Data>(XCB_WINDOW_NONE, Data::requestFunc(connection()))
    {
    }
};

/**
 * @brief Macro to create the WrapperData subclass.
 *
 * Creates a struct with name @p __NAME__ for the xcb request identified by
 * @p __REQUEST__ (the common prefix of the cookie type, reply type, request
 * function and reply function).
 */
#define XCB_WRAPPER_DATA(__NAME__, __REQUEST__, ...)                                                 \
    struct __NAME__ : public WrapperData<__REQUEST__##_reply_t, __REQUEST__##_cookie_t, __VA_ARGS__> \
    {                                                                                                \
        static constexpr request_func requestFunc = &__REQUEST__##_unchecked;                        \
        static constexpr reply_func replyFunc = &__REQUEST__##_reply;                                \
    };

/**
 * @brief Macro to create Wrapper typedef and WrapperData.
 *
 * Expands XCB_WRAPPER_DATA and creates an additional typedef for Wrapper with
 * name @p __NAME__.
 */
#define XCB_WRAPPER(__NAME__, __REQUEST__, ...)                \
    XCB_WRAPPER_DATA(__NAME__##Data, __REQUEST__, __VA_ARGS__) \
    typedef Wrapper<__NAME__##Data, __VA_ARGS__> __NAME__;

/**
 * @brief Wraps the xcb_intern_atom request.
 *
 * Upstream stores the name as QByteArray; the Qt-free port stores a
 * std::string.
 */
class Atom
{
public:
    explicit Atom(std::string &&name, bool onlyIfExists = false, xcb_connection_t *c = connection())
        : m_connection(c)
        , m_retrieved(false)
        , m_cookie(xcb_intern_atom_unchecked(m_connection, onlyIfExists, name.length(), name.c_str()))
        , m_atom(XCB_ATOM_NONE)
        , m_name(std::move(name))
    {
    }
    Atom() = delete;
    Atom(const Atom &) = delete;

    ~Atom()
    {
        if (!m_retrieved && m_cookie.sequence) {
            xcb_discard_reply(m_connection, m_cookie.sequence);
        }
    }

    operator xcb_atom_t() const
    {
        (const_cast<Atom *>(this))->getReply();
        return m_atom;
    }
    bool isValid()
    {
        getReply();
        return m_atom != XCB_ATOM_NONE;
    }
    bool isValid() const
    {
        (const_cast<Atom *>(this))->getReply();
        return m_atom != XCB_ATOM_NONE;
    }

    inline const std::string &name() const
    {
        return m_name;
    }

    void getReply()
    {
        if (m_retrieved || !m_cookie.sequence) {
            return;
        }
        std::unique_ptr<xcb_intern_atom_reply_t, decltype(&free)> reply(xcb_intern_atom_reply(m_connection, m_cookie, nullptr), &free);
        if (reply) {
            m_atom = reply->atom;
        }
        m_retrieved = true;
    }

private:
    xcb_connection_t *m_connection;
    bool m_retrieved;
    xcb_intern_atom_cookie_t m_cookie;
    xcb_atom_t m_atom;
    std::string m_name;
};

// ---- RandR family -----------------------------------------------------------

// local: the service layer resolves the primary output by itself; upstream
// has no wrapper for this request.
XCB_WRAPPER(OutputPrimary, xcb_randr_get_output_primary, xcb_window_t)

XCB_WRAPPER_DATA(CurrentResourcesData, xcb_randr_get_screen_resources_current, xcb_window_t)
class CurrentResources : public Wrapper<CurrentResourcesData, xcb_window_t>
{
public:
    explicit CurrentResources(WindowId window)
        : Wrapper<CurrentResourcesData, xcb_window_t>(window)
    {
    }

    inline xcb_randr_crtc_t *crtcs()
    {
        if (isNull()) {
            return nullptr;
        }
        return xcb_randr_get_screen_resources_current_crtcs(data());
    }
    inline xcb_randr_mode_info_t *modes()
    {
        if (isNull()) {
            return nullptr;
        }
        return xcb_randr_get_screen_resources_current_modes(data());
    }
    // local: mirrors upstream's ScreenResources accessors — the service layer
    // lists outputs from the current resources
    inline xcb_randr_output_t *outputs()
    {
        if (isNull()) {
            return nullptr;
        }
        return xcb_randr_get_screen_resources_current_outputs(data());
    }
    inline uint8_t *names()
    {
        if (isNull()) {
            return nullptr;
        }
        return xcb_randr_get_screen_resources_current_names(data());
    }
};

XCB_WRAPPER_DATA(CrtcInfoData, xcb_randr_get_crtc_info, xcb_randr_crtc_t, xcb_timestamp_t)
class CrtcInfo : public Wrapper<CrtcInfoData, xcb_randr_crtc_t, xcb_timestamp_t>
{
public:
    CrtcInfo() = default;
    CrtcInfo(const CrtcInfo &) = default;
    explicit CrtcInfo(xcb_randr_crtc_t c, xcb_timestamp_t t)
        : Wrapper<CrtcInfoData, xcb_randr_crtc_t, xcb_timestamp_t>(c, t)
    {
    }

    // local: upstream returns KWin's Rect (core/rect.h); this POD keeps the
    // Qt-free port self-contained
    struct Rect
    {
        int32_t x = 0;
        int32_t y = 0;
        uint16_t width = 0;
        uint16_t height = 0;
    };
    inline Rect rect()
    {
        const CrtcInfoData::reply_type *info = data();
        if (!info || info->num_outputs == 0 || info->mode == XCB_NONE || info->status != XCB_RANDR_SET_CONFIG_SUCCESS) {
            return Rect();
        }
        return Rect{info->x, info->y, info->width, info->height};
    }
    inline xcb_randr_output_t *outputs()
    {
        const CrtcInfoData::reply_type *info = data();
        if (!info || info->num_outputs == 0 || info->mode == XCB_NONE || info->status != XCB_RANDR_SET_CONFIG_SUCCESS) {
            return nullptr;
        }
        return xcb_randr_get_crtc_info_outputs(info);
    }
};

XCB_WRAPPER_DATA(OutputInfoData, xcb_randr_get_output_info, xcb_randr_output_t, xcb_timestamp_t)
class OutputInfo : public Wrapper<OutputInfoData, xcb_randr_output_t, xcb_timestamp_t>
{
public:
    OutputInfo() = default;
    OutputInfo(const OutputInfo &) = default;
    explicit OutputInfo(xcb_randr_output_t c, xcb_timestamp_t t)
        : Wrapper<OutputInfoData, xcb_randr_output_t, xcb_timestamp_t>(c, t)
    {
    }

    // upstream returned QString; the port returns std::string with the same
    // emptiness policy (an output driving no crtcs or modes has no name)
    inline std::string name()
    {
        const OutputInfoData::reply_type *info = data();
        if (!info || info->num_crtcs == 0 || info->num_modes == 0 || info->status != XCB_RANDR_SET_CONFIG_SUCCESS) {
            return std::string();
        }
        return std::string(reinterpret_cast<const char *>(xcb_randr_get_output_info_name(info)), info->name_len);
    }
};

} // namespace Xcb
} // namespace KWin

#endif
