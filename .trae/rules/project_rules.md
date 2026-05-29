# 项目规则

## 项目环境

- **操作系统**：Windows 10+
- **Qt 版本**：6.8.3
- **编译器**：MSVC 2022 (Windows)
- **构建系统**：CMake 3.30
- **构建器**：Ninja

## 工具路径

| 工具 | 路径 |
|------|------|
| Qt 6.8.3 | `C:\Qt\6.8.3\msvc2022_64\bin` |
| Qt Creator | `C:\Qt\Tools\QtCreator\bin` |
| CMake 3.30 | `C:\Qt\Tools\CMake_64\bin` |
| Ninja | `C:\Qt\Tools\Ninja` |
| MSVC 2022 | `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build` |

## 编译命令

```powershell
# 配置（首次或 CMakeLists.txt 改动后执行）
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" -B "d:\qt6cmake\NetShare\build\Desktop_Qt_6_8_3_MSVC2022_64bit-Release" -S "d:\qt6cmake\NetShare"

# 编译
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "d:\qt6cmake\NetShare\build\Desktop_Qt_6_8_3_MSVC2022_64bit-Release" --config Release
```

## 编码规范

- **所有源文件必须使用 UTF-8 编码**（包括 .cpp、.h、.qml、.html、.md、.cmake、CMakeLists.txt）
- 在 MSVC 编译时需确保源文件以 UTF-8 保存，避免中文注释或字符串乱码
- 编译脚本（cmd/powershell）也使用 UTF-8 编码

## 自动执行规则

在以下操作前必须自动读取本规则文件：
1. **创建**任何新文件
2. **修改**任何现有文件
3. **编译**代码

---

# 执行文档规范

创建执行文档时必须严格遵循以下规则：

## 0. 问题概述
先列出主要问题，简单介绍解决方法。

## 1. 执行步骤
列出执行步骤，编号 1、2、3...，步骤必须严格按顺序执行，禁止乱序执行。

## 2. 步骤格式
每步必须包含以下字段：
- **修改内容**：具体要修改什么文件、什么代码
- **难易程度**：高 / 中 / 低
- **完成状态**：未开始 / 进行中 / 阻塞 / 完成
- **验证方式**：必须验证通过才执行下一步；无法自动验证的需让用户确认

## 3. 状态更新
实时更新每步的状态，禁止等超过 2 步完成还不更新状态。

## 4. 验证要求
每步完成后必须验证，验证通过才能执行下一步。无法自动验证的步骤，必须询问用户确认后才能标记为完成。

## 5. 冲突和遗漏处理
冲突和遗漏不单独列出，必须直接整合到执行步骤中。遇到有歧义且没有最优选的情况时，给出选择建议让用户决定。