#pragma once

#include "grammar.h"

#include <climits>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// 表的管道：一行与 Arguments/ScreenBinding 之间的读写，加上"这行是否活跃"。
// 三个解释器（ToArgv / FromArgv / Validate）共用，所以它的边界写死在这儿：
// 只做取值与写入，不管顺序、不做校验、不拼字符串。
//
// 内务文件：不是模块的对外接口，lwe/ 之外不要包含。
namespace lwe {
namespace row {

using PropertyList = std::vector<std::pair<std::string, std::string>>;

// 一行的字段：类型匹配才有值。返回的是"指向成员指针的 const 指针"，
// 取值写法是 `object.*(*slot)` —— 成员指针的 const 只能加在指针本身上。
template <typename Member>
inline const Member* Slot(const Field& field) {
    return std::get_if<Member>(&field);
}

// ---- 读 --------------------------------------------------------------------

inline const bool* FlagValue(const Field& field, const Arguments& arguments) {
    const bool Arguments::* const* slot = Slot<bool Arguments::*>(field);
    return slot != nullptr ? &(arguments.*(*slot)) : nullptr;
}

inline const int* IntValue(const Field& field, const Arguments& arguments) {
    const int Arguments::* const* slot = Slot<int Arguments::*>(field);
    return slot != nullptr ? &(arguments.*(*slot)) : nullptr;
}

inline const std::string* TextValue(const Field& field, const Arguments& arguments) {
    const std::string Arguments::* const* slot = Slot<std::string Arguments::*>(field);
    return slot != nullptr ? &(arguments.*(*slot)) : nullptr;
}

inline const std::string* ScreenTextValue(const Field& field, const ScreenBinding& screen) {
    const std::string ScreenBinding::* const* slot = Slot<std::string ScreenBinding::*>(field);
    return slot != nullptr ? &(screen.*(*slot)) : nullptr;
}

inline const PropertyList* ListValue(const Field& field, const Arguments& arguments) {
    const PropertyList Arguments::* const* slot = Slot<PropertyList Arguments::*>(field);
    return slot != nullptr ? &(arguments.*(*slot)) : nullptr;
}

// 这行要不要发射 —— 不含互斥，互斥是组的事（见 toargv.cpp 的 Suppressed）。
//   Arity::Flag      看极性：Positive 为真发射，Negative 为假发射
//   Arity::Value     int 型没有"未设置"这一说（全显式发射）；字符串型空即不发射
//   Arity::Repeated  空列表不发射
inline bool Active(const OptionSpec& row, const Arguments& arguments) {
    switch (row.arity) {
    case Arity::Flag: {
        const bool* value = FlagValue(row.field, arguments);
        if (value == nullptr)
            return false;
        return row.polarity == Polarity::Negative ? !*value : *value;
    }
    case Arity::Value:
        if (IntValue(row.field, arguments) != nullptr)
            return true;
        if (const std::string* text = TextValue(row.field, arguments))
            return !text->empty();
        return false;
    case Arity::Repeated:
        if (const PropertyList* list = ListValue(row.field, arguments))
            return !list->empty();
        return false;
    }
    return false;
}

// ---- 写 --------------------------------------------------------------------
// 与读不同，这里成员指针的类型不能带 const（`const bool C::*` 是"const 成员"，
// 写不进去），const 只能加在指针本身上。

inline void WriteFlag(const Field& field, Arguments& arguments, bool value) {
    bool Arguments::* const* slot = Slot<bool Arguments::*>(field);
    if (slot != nullptr)
        arguments.*(*slot) = value;
}

inline void WriteInt(const Field& field, Arguments& arguments, int value) {
    int Arguments::* const* slot = Slot<int Arguments::*>(field);
    if (slot != nullptr)
        arguments.*(*slot) = value;
}

inline void WriteText(const Field& field, Arguments& arguments, const std::string& text) {
    std::string Arguments::* const* slot = Slot<std::string Arguments::*>(field);
    if (slot != nullptr)
        arguments.*(*slot) = text;
}

inline void WriteScreenText(const Field& field, ScreenBinding& screen, const std::string& text) {
    std::string ScreenBinding::* const* slot = Slot<std::string ScreenBinding::*>(field);
    if (slot != nullptr)
        screen.*(*slot) = text;
}

inline void WriteProperty(const Field& field, Arguments& arguments, const std::string& key,
                          const std::string& value) {
    PropertyList Arguments::* const* slot = Slot<PropertyList Arguments::*>(field);
    if (slot != nullptr)
        (arguments.*(*slot)).emplace_back(key, value);
}

// ---- 解析 ------------------------------------------------------------------

// 整串十进制：空串、非数字、溢出、尾随字符都算失败（引擎用 stoi/strtol，
// 我们只收下"整串都是数字"这一种）
inline bool ParseInt(const std::string& text, int* out) {
    if (text.empty())
        return false;
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end != text.c_str() + text.size())
        return false;
    if (value < INT_MIN || value > INT_MAX)
        return false;
    *out = static_cast<int>(value);
    return true;
}

} // namespace row
} // namespace lwe