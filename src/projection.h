#pragma once

#include "config.h"
#include "lwe/arguments.h"

// 投影：config::Config → lwe::Arguments。只做域映射（哪个配置字段喂哪个选项），
// 不碰语法 —— choices、绑定顺序、互斥、argv 拼写全部归 lwe::。
// 于是这一层既没有顺序也没有校验，可以随时重排、单独测。
//
// 投影里唯一带策略的一处：没有被启动的壁纸，它的属性在这里被丢掉
// （schemecolor 这类共享属性名不能从一张壁纸漏到另一张）。
namespace projection {

// 纯函数。空字段照原样传下去 —— "空 = 不发射"是表的语义
// （见 lwe/grammar.h），不是这里要判断的事。
lwe::Arguments ToArguments(const config::Config& config);

} // namespace projection