#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "common/Status.h"

// 能力边界：**DIB / BITMAP 的像素层**。
// 只处理 Win32 位图结构（头部解析、像素提取、像素哈希、保留位归一化），
// 不涉及剪贴板、不写文件、不知道"条目"是什么。
namespace clipd2d::core::clipboard {

// DIB 颜色表字节数（含 BI_BITFIELDS 的 3 个 DWORD 掩码）。
size_t DibColorTableBytes(const BITMAPINFOHEADER& header);

// 从 DIB 头部读宽高；失败返回 false。
bool ParseDibSize(const std::vector<std::uint8_t>& dib_bytes, int* width, int* height);

// 把 HBITMAP（DDB）快照成 32bpp top-down 的 CF_DIB 字节。
common::Status SnapshotBitmapToDibBytes(
    HBITMAP bitmap,
    std::vector<std::uint8_t>* dib_bytes,
    int* width,
    int* height);

// 取位图像素哈希（用于内容身份）。
common::Status ComputeBitmapPixelHash(
    HBITMAP bitmap,
    std::string* hash_out,
    int* width,
    int* height);

// 取出 HBITMAP 的 32bpp **straight BGRA** 像素（top-down）。
// 这是像素提取的唯一出口：哈希与编码都必须从这一份像素出发，
// 否则「算身份用的像素」和「落盘用的像素」会分叉。
common::Status ExtractBitmapPixels(
    HBITMAP bitmap,
    std::vector<std::uint8_t>* bgra,
    int* width,
    int* height);

// INVARIANT（与 NormalizeDibAlphaIfDegenerate 同一条规则的像素版）：
// 整张图 alpha 全 0 不是合法内容 —— 它是「生成方把保留位/未初始化内存当 alpha
// 写进 PNG」的签名（Snipaste / PixPin 等截图工具）。premultiplied 缓冲里 alpha
// 全 0 时 RGB 必为 0，而实测 RGB 是满量程，证明这份 alpha 不是真 alpha。
// 规则：仅当整张 alpha 全 0 时强制置 0xFF。返回是否做了改写。
bool ForceOpaqueIfAlphaDegenerate(int width, int height, std::uint8_t* bgra, size_t stride);

// INVARIANT: CF_DIB（BI_RGB, 32bpp）的第 4 字节按 Win32 规范是保留位，GDI 与大量
// 第三方截图工具（Snipaste / PixPin 等）统一把它填 0。若原样当成 alpha 送去编码，
// 得到的 PNG 会整张全透明 —— 用户看到的就是「截图是空白的」。
// 规则：仅当 DIB 未声明 alpha 掩码、且所有像素的第 4 字节都为 0 时，判定该通道不是
// alpha，强制置为不透明。真实带 alpha 的 DIBV5 不受影响。幂等，可重复调用。
void NormalizeDibAlphaIfDegenerate(std::vector<std::uint8_t>* dib_bytes);

}  // namespace clipd2d::core::clipboard
