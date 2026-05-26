#
# NetShare Web 前端模块
# 提供移动端访问的 HTML/CSS/JS 界面
#

# =============================================================================
# Web 资源目录
# =============================================================================
set(WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# =============================================================================
# HTML 文件
# =============================================================================
set(WEB_HTML_FILES
    index.html
    upload.html
    receive.html
)

# =============================================================================
# CSS 文件 (可选)
# =============================================================================
set(WEB_CSS_DIR ${WEB_DIR}/css)
set(WEB_CSS_FILES)

# =============================================================================
# JavaScript 文件 (可选)
# =============================================================================
set(WEB_JS_DIR ${WEB_DIR}/js)
set(WEB_JS_FILES)

# =============================================================================
# 图片资源
# =============================================================================
set(WEB_IMAGES_DIR ${WEB_DIR}/images)
set(WEB_IMAGE_FILES)

# =============================================================================
# 创建资源文件列表
# =============================================================================
set(WEB_ASSETS_LIST "${CMAKE_CURRENT_BINARY_DIR}/web_assets.txt")

file(WRITE ${WEB_ASSETS_LIST} "# NetShare Web Assets\n")
file(APPEND ${WEB_ASSETS_LIST} "# Generated at: ${CMAKE_CURRENT_TIME}\n\n")

foreach(file ${WEB_HTML_FILES})
    file(APPEND ${WEB_ASSETS_LIST} "${WEB_DIR}/${file}\n")
endforeach()

# =============================================================================
# 自定义目标：复制 Web 资源
# =============================================================================
add_custom_target(netshare_web_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${WEB_DIR}
        $<TARGET_FILE_DIR:NetShare>/web
    DEPENDS NetShare
    COMMENT "Copying web assets to build directory"
    VERBATIM
)

# =============================================================================
# 安装规则
# =============================================================================
install(DIRECTORY ${WEB_DIR}/
    DESTINATION share/NetShare/web
    FILES_MATCHING
        PATTERN "*.html"
        PATTERN "*.css"
        PATTERN "*.js"
        PATTERN "*.png"
        PATTERN "*.jpg"
        PATTERN "*.svg"
)

message(STATUS "  Web module: netshare_web_assets")
