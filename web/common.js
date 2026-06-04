/*
 * NetShare Web Common Module
 * Shared i18n translations and utility functions
 */

// ===== i18n Translation Table =====
var NetShareI18n = {
    zh: {
        // Common
        powered_by: '由 NetShare 提供',
        select_file: '📄 选择文件',
        select_folder: '📁 选择文件夹',
        network_error: '网络错误',
        js_error: 'JS错误: ',
        promise_error: 'Promise错误: ',

        // Home page
        service: '局域网文件分享服务',
        use_link: '请使用分享链接访问具体分享内容',

        // Share/Download page
        title_share: 'NetShare - 文件分享',
        folder_share: '文件夹分享',
        file_share: '文件分享',
        download: '下载',
        download_zip: '打包下载 (ZIP)',
        download_file: '下载文件',
        file_list: '文件列表',
        name: '名称',
        size: '大小',
        action: '操作',
        upload_file: '上传文件',
        preparing: '准备中...',
        cancel: '取消',
        fetching_info: '获取文件信息...',
        fetch_failed: '获取文件信息失败',
        no_resume: '服务器不支持断点续传，使用直接下载',
        large_file: '文件较大',
        suggest_manager: '建议使用下载管理器',
        file_size: '文件大小: ',
        error: '错误: ',
        resuming: '续传中... ',
        downloaded: ' 已下载',
        downloading: '下载中...',
        dl_failed: '下载失败: HTTP ',
        interrupted: '下载中断，点击下载按钮继续',
        cancelled_resume: '已取消，点击下载按钮继续',
        interrupted_msg: '下载中断: ',
        saving: '保存中...',
        complete: '下载完成!',
        loading: '加载中...',

        // Password page
        pwd_title: '密码验证',
        pwd_required: '需要密码',
        enter_pwd: '此分享需要密码才能访问',
        pwd_placeholder: '请输入密码',
        verify: '验证',

        // Error page
        error_share_not_exist: '分享不存在',
        error_share_invalid: '此分享链接无效或已过期',

        // Upload page
        upload_title: '上传文件',
        upload_subtitle: '选择文件或文件夹上传到局域网分享',
        drag_here: '将文件或文件夹拖拽到此处',
        uploading: '上传中...',
        start_upload: '开始上传',
        upload_success: '上传成功！',
        upload_files: '上传 ',
        files_unit: ' 个文件 (',
        prepare_upload: '准备上传...',
        upload_complete: '上传完成',
        view_share: '查看分享页面',
        uploading_n: '正在上传 ',
        upload_failed: '上传失败: ',

        // Receive page
        title_send: 'NetShare - 发送文件',
        send_file: '📤 发送文件',
        send_file_subtitle: '选择文件发送到对方设备',
        drag_drop_hint: '将文件或文件夹拖拽到此处',
        sending: '发送中...',
        start_send: '开始发送',
        retry_failed: '重试失败文件',
        send_success: '文件发送成功！',
        send_message: '💬 发送消息',
        input_message: '输入消息内容...',
        send: '发送',
        unknown_size: '未知大小',
        file_count: '共 %1 个文件，总大小 %2',
        upload_paused: '上传已暂停',
        files_failed: '%1 个文件发送失败',
        completed: '已完成',
        failed: '失败: ',
        disconnected: '与服务端断开连接，请重试',
        live_connection: '实时连接',
        connection_lost: '连接断开',
        conn_broken: '连接断开',
        paused: '已暂停',
        check_error: '检查接口返回错误: HTTP ',
        check_failed: '检查上传状态失败: ',
        upload_error: '上传过程出错: ',
        resume_detected: '检测到未完成的上传，',
        skip_files: '跳过%1个已完成文件',
        resume_files: '续传%1个部分文件',
        upload_paused_err: '上传已暂停',
        network_error_detail: '网络错误: ',
        timeout: '超时: ',
        resume_failed: '续传失败，服务端连续重启超过%1次，请重新上传',
        unfinished_tasks: '检测到 %1 个未完成的上传任务，请选择相同文件后点击开始发送续传',
        pending_tasks: '有未完成的任务，请选择文件后续传',
        retry_check_failed: '重试检查失败: ',
        finalizing: '正在完成...',
        sent_count: '成功发送 %1 个文件！',
        finalize_failed: '完成操作失败',
        msg_send_failed: '消息发送失败',
        msg_send_error: '消息发送失败: '
    },
    en: {
        // Common
        powered_by: 'Powered by NetShare',
        select_file: '📄 Select Files',
        select_folder: '📁 Select Folder',
        network_error: 'Network error',
        js_error: 'JS Error: ',
        promise_error: 'Promise Error: ',

        // Home page
        service: 'LAN File Sharing Service',
        use_link: 'Please use a share link to access specific content',

        // Share/Download page
        title_share: 'NetShare - File Share',
        folder_share: 'Folder Share',
        file_share: 'File Share',
        download: 'Download',
        download_zip: 'Download (ZIP)',
        download_file: 'Download File',
        file_list: 'File List',
        name: 'Name',
        size: 'Size',
        action: 'Action',
        upload_file: 'Upload File',
        preparing: 'Preparing...',
        cancel: 'Cancel',
        fetching_info: 'Fetching file info...',
        fetch_failed: 'Failed to fetch file info',
        no_resume: 'Server does not support resume, using direct download',
        large_file: 'Large file',
        suggest_manager: 'a download manager is recommended',
        file_size: 'File size: ',
        error: 'Error: ',
        resuming: 'Resuming... ',
        downloaded: ' downloaded',
        downloading: 'Downloading...',
        dl_failed: 'Download failed: HTTP ',
        interrupted: 'Download interrupted, click download to continue',
        cancelled_resume: 'Cancelled, click download to continue',
        interrupted_msg: 'Download interrupted: ',
        saving: 'Saving...',
        complete: 'Download complete!',
        loading: 'Loading...',

        // Password page
        pwd_title: 'Password Verification',
        pwd_required: 'Password Required',
        enter_pwd: 'This share requires a password',
        pwd_placeholder: 'Enter password',
        verify: 'Verify',

        // Error page
        error_share_not_exist: 'Share Not Found',
        error_share_invalid: 'This share link is invalid or has expired',

        // Upload page
        upload_title: 'Upload File',
        upload_subtitle: 'Select files or folders to upload to LAN share',
        drag_here: 'Drag files or folders here',
        uploading: 'Uploading...',
        start_upload: 'Start Upload',
        upload_success: 'Upload successful!',
        upload_files: 'Upload ',
        files_unit: ' files (',
        prepare_upload: 'Preparing upload...',
        upload_complete: 'Upload complete',
        view_share: 'View share page',
        uploading_n: 'Uploading ',
        upload_failed: 'Upload failed: ',

        // Receive page
        title_send: 'NetShare - Send Files',
        send_file: '📤 Send Files',
        send_file_subtitle: 'Select files to send to the other device',
        drag_drop_hint: 'Drag and drop files or folders here',
        sending: 'Sending...',
        start_send: 'Start Sending',
        retry_failed: 'Retry Failed Files',
        send_success: 'Files sent successfully!',
        send_message: '💬 Send Message',
        input_message: 'Type a message...',
        send: 'Send',
        unknown_size: 'Unknown size',
        file_count: '%1 files, total %2',
        upload_paused: 'Upload paused',
        files_failed: '%1 file(s) failed to send',
        completed: 'Completed',
        failed: 'Failed: ',
        disconnected: 'Disconnected from server, please retry',
        live_connection: 'Live',
        connection_lost: 'Connection lost',
        conn_broken: 'Connection broken',
        paused: 'Paused',
        check_error: 'Check API error: HTTP ',
        check_failed: 'Failed to check upload status: ',
        upload_error: 'Upload error: ',
        resume_detected: 'Unfinished upload detected, ',
        skip_files: 'Skipped %1 completed file(s)',
        resume_files: 'Resuming %1 partial file(s)',
        upload_paused_err: 'Upload paused',
        network_error_detail: 'Network error: ',
        timeout: 'Timeout: ',
        resume_failed: 'Resume failed, server restarted more than %1 times, please re-upload',
        unfinished_tasks: '%1 unfinished upload task(s) detected, select the same files and click Start to resume',
        pending_tasks: 'Unfinished tasks exist, select files to resume',
        retry_check_failed: 'Retry check failed: ',
        finalizing: 'Finalizing...',
        sent_count: 'Successfully sent %1 file(s)!',
        finalize_failed: 'Finalize failed',
        msg_send_failed: 'Failed to send message',
        msg_send_error: 'Failed to send message: '
    }
};

// ===== Global i18n State =====
var currentLang = 'zh';

// ===== Translation Functions =====
function t(key) {
    return (NetShareI18n[currentLang] && NetShareI18n[currentLang][key]) || (NetShareI18n.zh[key]) || key;
}

function tf(key) {
    var args = Array.prototype.slice.call(arguments, 1);
    var str = t(key);
    for (var i = 0; i < args.length; i++) {
        str = str.replace('%' + (i + 1), args[i]);
    }
    return str;
}

// ===== Apply i18n to DOM =====
function applyI18n(callback) {
    var params = new URLSearchParams(window.location.search);
    var urlLang = params.get('lang');
    if (urlLang && NetShareI18n[urlLang]) {
        currentLang = urlLang;
        doApplyI18n();
        if (callback) callback();
    } else {
        fetch('/api/language').then(function(r) { return r.json(); }).then(function(data) {
            currentLang = (data.language && NetShareI18n[data.language]) ? data.language : 'zh';
            doApplyI18n();
            if (callback) callback();
        }).catch(function() {
            currentLang = 'zh';
            doApplyI18n();
            if (callback) callback();
        });
    }
}

function doApplyI18n() {
    document.documentElement.lang = currentLang === 'en' ? 'en' : 'zh-CN';
    document.querySelectorAll('[data-i18n]').forEach(function(el) {
        var key = el.getAttribute('data-i18n');
        if (NetShareI18n[currentLang] && NetShareI18n[currentLang][key] !== undefined) {
            el.textContent = NetShareI18n[currentLang][key];
        }
    });
    document.querySelectorAll('[data-i18n-placeholder]').forEach(function(el) {
        var key = el.getAttribute('data-i18n-placeholder');
        if (NetShareI18n[currentLang] && NetShareI18n[currentLang][key] !== undefined) {
            el.placeholder = NetShareI18n[currentLang][key];
        }
    });
}

// ===== Utility Functions =====
function formatSize(bytes) {
    if (!bytes || bytes === 0) return '--';
    var k = 1024;
    var s = ['B', 'KB', 'MB', 'GB', 'TB'];
    var i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + s[i];
}

function escapeHtml(text) {
    var d = document.createElement('div');
    d.textContent = text;
    return d.innerHTML;
}
