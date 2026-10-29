cmake_minimum_required(VERSION 3.28)

project(Scraps VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT WIN32)
  message(FATAL_ERROR "Scraps 当前只支持 Windows。")
endif()

# ---------------------------------------------------------------------------
# 分层 target：foundation → domain → ui → (app | cli)
#   common/                  -> scraps_foundation
#   core/ + ipc/ + platform/ -> scraps_domain
#   ui/                      -> scraps_ui（含 yoga）
#   screenshot/              -> 独立进程，只链系统库与 third_party
# target 拆分让改动爆炸半径可控：改 ui/ 头不重编 domain/foundation（实测
# touch ui/MainWindow.h 后 scraps_domain 零编译）。include 层次由
# scripts/check_layering.py 门禁强制——src/ 根是共享的，靠 include path 拦不住
# core 偷 include ui，必须用 lint。分层见 docs/plans/architecture-agent-readiness.md 第 1 步。
# ---------------------------------------------------------------------------

set(SCRAPS_FOUNDATION_SOURCES
  src/common/ClipboardSuppression.cpp
  src/common/FileHash.cpp
  src/common/Log.cpp
  src/common/Paths.cpp
  src/common/StringUtil.cpp
  src/common/Utf8.cpp
  src/common/AsyncRunner.cpp
  src/common/StartupTrace.cpp
  src/common/ThreadPool.cpp
)

set(SCRAPS_DOMAIN_SOURCES
  src/core/actions/ActionResolver.cpp
  src/core/backup/BackupService.cpp
  src/core/browser/ClipBrowserService.cpp
  src/core/browser/ClipQueryBuilder.cpp
  src/core/browser/QuickPasteQuery.cpp
  src/core/clipboard/ClipboardCapture.cpp
  src/core/clipboard/ClipboardDiagnostics.cpp
  src/core/clipboard/ClipboardFormat.cpp
  src/core/clipboard/ClipboardRead.cpp
  src/core/clipboard/ClipboardSession.cpp
  src/core/clipboard/ClipboardWriter.cpp
  src/core/clipboard/ContentProfile.cpp
  src/core/clipboard/ImageDib.cpp
  src/core/clipboard/ImageIdentity.cpp
  src/core/clipboard/ImagePng.cpp
  src/core/clipboard/ImageWriteback.cpp
  src/core/clipboard/OpenTargetResolver.cpp
  src/core/clipboard/PasteDispatch.cpp
  src/core/clipboard/PasteTarget.cpp
  src/core/clipboard/ProcessInfo.cpp
  src/core/ocr/ImageOcrProvider.cpp
  src/core/qr/UrlQrGenerator.cpp
  src/core/commands/AppController.Clipboard.cpp
  src/core/commands/AppController.Ingest.cpp
  src/core/commands/AppController.Library.cpp
  src/core/commands/AppController.Ocr.cpp
  src/core/commands/AppController.Paste.cpp
  src/core/commands/Command.cpp
  src/core/commands/CommandService.cpp
  src/core/paste/TextTransformEngine.cpp
  src/core/policies/ClipIdentityPolicy.cpp
  src/core/policies/ImageIngestPolicy.cpp
  src/core/policies/ClipboardRetryPolicy.cpp
  src/core/store/ClipRepository.Schema.cpp
  src/core/store/ClipRepository.Identity.cpp
  src/core/store/ClipRepository.Query.cpp
  src/core/store/ClipRepository.ClipMutations.cpp
  src/core/store/ClipRepository.Groups.cpp
  src/core/store/ClipRepository.Insert.cpp
  src/core/store/ClipRepository.Queue.cpp
  src/core/store/ClipRepository.Settings.cpp
  src/core/store/ClipRepository.Tags.cpp
  src/core/store/ClipRepository.Assets.cpp
  src/core/store/SettingsRepository.cpp
  src/ipc/NamedPipeClient.cpp
  src/ipc/NamedPipeProtocol.cpp
  src/ipc/NamedPipeServer.cpp
  src/platform/win32/ClipboardEventReceiver.cpp
  src/platform/win32/HotkeySpec.cpp
  src/platform/win32/NotifyIconHost.cpp
  src/platform/win32/ShellMessageWindow.cpp
  third_party/qrcodegen/qrcodegen.c
)

set(SCRAPS_UI_SOURCES
  src/ui/D2dShell.cpp
  src/ui/ClipImagePreview.cpp
  src/ui/ClipRowText.cpp
  src/ui/ClipVisualFileContent.cpp
  src/ui/D2DWindowlessTextBox.cpp
  src/ui/text/GapBuffer.cpp
  src/ui/DrawTextUtil.cpp
  src/ui/DialogPanelWindow.cpp
  src/ui/FluentIcons.cpp
  src/ui/HoverHintWindow.cpp
  src/ui/layout/FlexNode.cpp
  src/ui/vlist/HeightIndex.cpp
  src/ui/vlist/StableSlotPool.cpp
  src/ui/vlist/RowHeightModel.cpp
  src/ui/vlist/VirtualListViewport.cpp
  src/ui/ClipVisualAssets.cpp
  src/ui/HistoryAssetLoader.cpp
  src/ui/HistoryDataController.cpp
  src/ui/PanelFocusCoordinator.cpp
  src/ui/SettingsWindow.cpp
  src/ui/settings/SettingsDraftState.cpp
  src/ui/settings/SettingsSearchController.cpp
  src/ui/settings/SettingsDrawUtil.cpp
  src/ui/settings/SettingsPanelGeneral.cpp
  src/ui/settings/SettingsPanelAppearance.cpp
  src/ui/settings/SettingsPanelHotkey.cpp
  src/ui/settings/SettingsPanelClipboard.cpp
  src/ui/settings/SettingsPanelFilter.cpp
  src/ui/settings/SettingsPanelData.cpp
  src/ui/settings/SettingsPanelAbout.cpp
  src/ui/ListScrollController.cpp
  src/ui/MainWindow.cpp
  src/ui/MainWindow.autohide.cpp
  src/ui/MainWindow.actions.cpp
  src/ui/MainWindow.bench.cpp
  src/ui/MainWindow.data.cpp
  src/ui/MainWindow.dialogs.cpp
  src/ui/MainWindow.drag.cpp
  src/ui/MainWindow.input.cpp
  src/ui/MainWindow.menu.cpp
  src/ui/MainWindow.paste.cpp
  src/ui/MainWindow.selection.cpp
  src/ui/QuickPasteWindow.cpp
  src/ui/MainWindow.render.cpp
  src/ui/PreviewContentModel.cpp
  src/ui/PreviewWindow.cpp
  src/ui/TextLayoutCache.cpp
)

file(GLOB_RECURSE YOGA_SOURCES "third_party/yoga/*.cpp")

# ---- common: 纯工具，不依赖业务层 ----
add_library(scraps_foundation STATIC ${SCRAPS_FOUNDATION_SOURCES})
target_include_directories(scraps_foundation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(
  scraps_foundation
  PUBLIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SCRAPS_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(
  scraps_foundation
  PUBLIC
    advapi32
    bcrypt
    gdi32
    ole32
    shell32
    user32
)

# ---- yoga: 仅 ui/layout/FlexNode 使用 ----
add_library(scraps_yoga STATIC ${YOGA_SOURCES})
target_include_directories(
  scraps_yoga
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# ---- core + ipc + platform: 业务域 ----
add_library(scraps_domain STATIC ${SCRAPS_DOMAIN_SOURCES})
target_include_directories(
  scraps_domain
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qrcodegen
)
target_compile_definitions(scraps_domain PRIVATE SCRAPS_ENABLE_TEST_HOOKS)
target_link_libraries(
  scraps_domain
  PUBLIC
    scraps_foundation
    Psapi
    runtimeobject
    winsqlite3
)

# ---- ui: 渲染与窗口，依赖 domain ----
add_library(scraps_ui STATIC ${SCRAPS_UI_SOURCES})
target_include_directories(
  scraps_ui
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # WebView2 SDK（头 + 静态 Loader）：预览窗 HTML 富文本渲染，懒加载不进主路径
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/include
)
target_compile_definitions(scraps_ui PRIVATE SCRAPS_ENABLE_TEST_HOOKS)
target_link_libraries(
  scraps_ui
  PUBLIC
    scraps_domain
    scraps_yoga
    comctl32
    d2d1
    d3d11
    dxguid
    dwrite
    dwmapi
    imm32
    winmm
    windowscodecs
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/lib/x64/WebView2LoaderStatic.lib
)

set(SCRAPS_LIB_TARGETS scraps_foundation scraps_yoga scraps_domain scraps_ui)

# ---- 主程序 ----
add_executable(
  Scraps WIN32
  src/app/main.cpp
  src/app/Application.cpp
  src/app/LaunchPolicy.cpp
  src/app/QuickPasteSession.cpp
  src/app/ScreenshotHostManager.cpp
  src/app/UpdateChecker.cpp
  src/app/app.rc
)
target_include_directories(Scraps PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(Scraps PRIVATE scraps_ui dbghelp winhttp avrt)

if(MSVC)
  target_link_options(Scraps PRIVATE
    "/DELAYLOAD:d3d11.dll"
    "/DELAYLOAD:d2d1.dll"
    "/DELAYLOAD:dcomp.dll"
    "/DELAYLOAD:dwrite.dll"
    "delayimp.lib"
  )
endif()

add_custom_command(
  TARGET Scraps POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Scraps>/assets/fluent
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fluent
    $<TARGET_FILE_DIR:Scraps>/assets/fluent
)

# ---- CLI：走 Named Pipe 命令通道；窗口类名/消息 ID 走 ui/WindowContract.h，
#      因此不再链接 scraps_ui，也不必编译整个 UI 层 ----
add_executable(scraps-cli src/cli/main.cpp src/cli/CliCommandParser.cpp src/app/app.rc)
target_link_libraries(scraps-cli PRIVATE scraps_domain)

# ---- 截图子进程：对 src/ 其它模块零 include，只链系统库 ----
set(SCRAPS_SCREENSHOT_SOURCES
  src/screenshot/app/main.cpp
  src/screenshot/app/ScreenshotApp.cpp
  src/screenshot/app/ScreenshotSession.cpp
  src/screenshot/capture/CaptureSupervisor.cpp
  src/screenshot/capture/DxgiDuplicationBackend.cpp
  src/screenshot/capture/GdiCaptureBackend.cpp
  src/screenshot/capture/WgcCaptureBackend.cpp
  src/screenshot/editing/AnnotationDocument.cpp
  src/screenshot/editing/CommandManager.cpp
  src/screenshot/editing/InteractionStateMachine.cpp
  src/screenshot/editing/SelectionModel.cpp
  src/screenshot/editing/ToolbarUI.cpp
  src/screenshot/export/PngExporter.cpp
  src/screenshot/export/RoiComposer.cpp
  src/screenshot/overlay/DCompDeviceResources.cpp
  src/screenshot/overlay/OverlayRenderer.cpp
  src/screenshot/overlay/OverlayWindowManager.cpp
  src/screenshot/pin/PinWindow.cpp
  src/screenshot/pin/PinWindowManager.cpp
  src/screenshot/platform/CoordinateMapper.cpp
  src/screenshot/platform/MonitorTopology.cpp
  src/screenshot/platform/UiaSnappingService.cpp
  src/screenshot/platform/WindowEnumerationService.cpp
  src/screenshot/protocol/ScreenshotProtocol.cpp
)

add_executable(scraps_screenshot WIN32 ${SCRAPS_SCREENSHOT_SOURCES} src/app/app.rc)
target_compile_definitions(
  scraps_screenshot
  PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    SCRAPS_VERSION="${PROJECT_VERSION}"
)
target_include_directories(
  scraps_screenshot
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/screenshot
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(
  scraps_screenshot
  PRIVATE
    comctl32
    d2d1
    d3d11
    dxguid
    dcomp
    dwrite
    dwmapi
    gdi32
    ole32
    shell32
    user32
    windowscodecs
)

# ---- 测试 ----
enable_testing()
add_executable(
  scraps_tests
  src/app/LaunchPolicy.cpp
  src/tests/TestMain.cpp
  src/tests/ClipIdentityPolicyTests.cpp
  src/tests/ImageIngestPolicyTests.cpp
  src/tests/ClipboardRetryPolicyTests.cpp
  src/tests/ClipRepositoryMutationTests.cpp
  src/tests/ClipRepositoryTransactionTests.cpp
  src/tests/ClipboardDibAlphaTests.cpp
  src/tests/LaunchPolicyTests.cpp
  src/tests/AutoHideInteractionStateTests.cpp
  src/tests/BrowserStateTests.cpp
  src/tests/AppControllerResolveActionsTests.cpp
  src/tests/AppControllerClipboardPolicyTests.cpp
  src/tests/AppControllerLifecycleTests.cpp
  src/tests/PreviewWindowTests.cpp
  src/tests/PreviewContentModelTests.cpp
  src/tests/RefreshRequestTests.cpp
  src/tests/RowHeightConsistencyTests.cpp
  src/tests/TextTransformEngineTests.cpp
  src/tests/UrlQrGeneratorTests.cpp
  src/tests/OpenTargetResolverTests.cpp
  src/tests/ClipQueryBuilderTests.cpp
  src/tests/ClipImagePreviewTests.cpp
  src/tests/ClipVisualFileContentTests.cpp
  src/tests/GroupMenuCommandTests.cpp
  src/tests/HistoryViewStateTests.cpp
  src/tests/HotkeySpecTests.cpp
  src/tests/NamedPipeProtocolTests.cpp
  src/tests/ClipboardWriterTests.cpp
  src/tests/VirtualListTests.cpp
  src/tests/StopwatchTests.cpp
  src/tests/ScreenshotRoiComposerTests.cpp
  src/tests/ScreenshotCoordinateMapperTests.cpp
  src/tests/ScreenshotInteractionStateMachineTests.cpp
  src/tests/SettingsDraftStateTests.cpp
  src/tests/SettingsSearchControllerTests.cpp
  src/tests/PasteQueueModelTests.cpp
  # screenshot 纯逻辑源：测试直编（screenshot 是独立进程 target，不进 scraps_ui）
  src/screenshot/export/RoiComposer.cpp
  src/screenshot/platform/CoordinateMapper.cpp
  src/screenshot/platform/MonitorTopology.cpp
  src/screenshot/platform/WindowEnumerationService.cpp
  src/screenshot/platform/UiaSnappingService.cpp
  src/screenshot/editing/SelectionModel.cpp
  src/screenshot/editing/ToolbarUI.cpp
  src/screenshot/editing/AnnotationDocument.cpp
  src/screenshot/editing/InteractionStateMachine.cpp
)
target_link_libraries(scraps_tests PRIVATE scraps_ui)
target_compile_definitions(scraps_tests PRIVATE SCRAPS_ENABLE_TEST_HOOKS)
target_include_directories(scraps_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/screenshot)
add_test(NAME scraps_tests COMMAND scraps_tests)

set(SCRAPS_ALL_TARGETS
  ${SCRAPS_LIB_TARGETS}
  Scraps
  scraps-cli
  scraps_screenshot
  scraps_tests
)

if(MSVC)
  foreach(target_name IN LISTS SCRAPS_ALL_TARGETS)
    target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(${target_name} PRIVATE /sdl /guard:cf)
    target_compile_options(${target_name} PRIVATE $<$<CONFIG:Release>:/Zi>)
  endforeach()
  foreach(target_name IN ITEMS Scraps scraps-cli scraps_screenshot scraps_tests)
    target_link_options(${target_name} PRIVATE /GUARD:CF /DYNAMICBASE /NXCOMPAT)
    target_link_options(${target_name} PRIVATE $<$<CONFIG:Release>:/DEBUG>)
  endforeach()
else()
  foreach(target_name IN LISTS SCRAPS_ALL_TARGETS)
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endforeach()
endif()

if(MSVC)
  set_property(TARGET Scraps PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_property(TARGET scraps-cli PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_property(TARGET scraps_tests PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_property(TARGET scraps_screenshot PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
