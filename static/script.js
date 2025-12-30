document.addEventListener('DOMContentLoaded', () => {
    const dropZone = document.getElementById('drop-zone');
    const fileListEl = document.getElementById('file-list');
    const startBtn = document.getElementById('start-convert');
    const clearBtn = document.getElementById('clear-list');
    const openFolderBtn = document.getElementById('open-folder');
    const progressText = document.getElementById('progress-info');
    const progressFill = document.getElementById('progress-fill');

    // 时钟更新
    setInterval(() => {
        const now = new Date();
        document.getElementById('clock').innerText = now.toLocaleTimeString('zh-CN', { hour12: false });
    }, 1000);

    let filesQueue = [];

    // ==================== 页面导航 ====================
    const navTabs = document.querySelectorAll('.nav-tab');
    navTabs.forEach(tab => {
        tab.addEventListener('click', () => {
            // 切换 tab 激活状态
            navTabs.forEach(t => t.classList.remove('active'));
            tab.classList.add('active');

            // 切换页面显示
            const pageId = 'page-' + tab.dataset.page;
            document.querySelectorAll('.page-content').forEach(page => {
                page.style.display = 'none';
            });
            document.getElementById(pageId).style.display = '';

            // 如果切换到归档页面，自动刷新
            if (tab.dataset.page === 'archive') {
                loadArchive();
            }
        });
    });

    // ==================== 文件拖拽处理 ====================
    dropZone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.classList.add('drag-over');
    });

    dropZone.addEventListener('dragleave', () => {
        dropZone.classList.remove('drag-over');
    });

    dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropZone.classList.remove('drag-over');
        handleFiles(e.dataTransfer.files);
    });

    // 单文件选择
    document.getElementById('file-input').addEventListener('change', (e) => {
        handleFiles(e.target.files);
        e.target.value = '';
    });

    // 文件夹选择
    document.getElementById('folder-input').addEventListener('change', (e) => {
        handleFiles(e.target.files);
        e.target.value = '';
    });

    function handleFiles(files) {
        let addedCount = 0;
        Array.from(files).forEach(file => {
            // 过滤已存在的文件
            if (filesQueue.some(f => f.file.name === file.name && f.file.size === file.size)) return;

            const ext = file.name.split('.').pop().toLowerCase();
            if (['kgg', 'kgm', 'kgma', 'flac', 'ogg'].includes(ext)) {
                filesQueue.push({
                    file: file,
                    id: Math.random().toString(36).substr(2, 9),
                    status: 'pending'
                });
                addedCount++;
            }
        });
        if (addedCount > 0) {
            renderList();
            // 添加系统消息
            addSystemMessage(`已添加 ${addedCount} 个文件到队列`);
        }
    }

    function addSystemMessage(msg) {
        const div = document.createElement('div');
        div.className = 'log-line system-msg';
        div.innerHTML = `
            <span class="timestamp">[SYS]</span>
            <span class="message">${msg}</span>
        `;
        fileListEl.appendChild(div);
        fileListEl.scrollTop = fileListEl.scrollHeight;
    }

    // ==================== 文件列表渲染 ====================
    function renderList() {
        // 保留系统消息
        const systemMsgs = Array.from(fileListEl.querySelectorAll('.system-msg'));
        fileListEl.innerHTML = '';
        systemMsgs.forEach(msg => fileListEl.appendChild(msg));

        filesQueue.forEach((item) => {
            const div = document.createElement('div');
            let statusClass = 'status-pending';
            let statusBadgeClass = 'wait';
            let statusText = '等待中';

            if (item.status === 'processing') {
                statusClass = 'status-processing';
                statusBadgeClass = 'proc';
                statusText = '解码中...';
            } else if (item.status === 'done') {
                statusClass = 'status-success';
                statusBadgeClass = 'ok';
                statusText = '完成';
            } else if (item.status === 'error') {
                statusClass = 'status-error';
                statusBadgeClass = 'err';
                statusText = '失败';
            }

            div.className = `log-line ${statusClass}`;
            div.innerHTML = `
                <span class="file-icon">::</span>
                <span class="log-file" title="${item.file.name}">${item.file.name}</span>
                <span class="status-badge ${statusBadgeClass}">${statusText}</span>
                ${item.status === 'pending' || item.status === 'error' ?
                    `<span class="delete-btn" onclick="removeFile('${item.id}')">[x]</span>` : ''}
            `;
            fileListEl.appendChild(div);
        });

        fileListEl.scrollTop = fileListEl.scrollHeight;

        const hasPending = filesQueue.some(f => f.status === 'pending');
        const isProcessing = filesQueue.some(f => f.status === 'processing');
        startBtn.disabled = !hasPending || isProcessing;
    }

    window.removeFile = function (id) {
        filesQueue = filesQueue.filter(f => f.id !== id);
        renderList();
    };

    clearBtn.addEventListener('click', () => {
        if (filesQueue.some(f => f.status === 'processing')) return;
        filesQueue = [];
        // 清空非系统消息
        fileListEl.innerHTML = `
            <div class="log-line system-msg">
                <span class="timestamp">[SYS]</span>
                <span class="message">队列已清空</span>
            </div>
        `;
        progressText.innerText = "STANDBY";
        progressFill.style.width = "0%";
        startBtn.disabled = true;
    });

    // ==================== 转换处理 ====================
    startBtn.addEventListener('click', async () => {
        const pendingFiles = filesQueue.filter(f => f.status === 'pending' || f.status === 'error');
        if (pendingFiles.length === 0) return;

        const format = document.querySelector('input[name="format"]:checked').value;
        startBtn.disabled = true;

        for (let i = 0; i < pendingFiles.length; i++) {
            const item = pendingFiles[i];
            item.status = 'processing';
            renderList();

            const progress = Math.round(((i) / pendingFiles.length) * 100);
            progressFill.style.width = `${progress}%`;
            progressText.innerText = `[${i + 1}/${pendingFiles.length}] ${item.file.name}`;

            try {
                const formData = new FormData();
                formData.append('file', item.file);
                formData.append('format', format);

                const response = await fetch('/api/convert', {
                    method: 'POST',
                    body: formData
                });
                const result = await response.json();

                item.status = result.success ? 'done' : 'error';
                if (!result.success) {
                    addSystemMessage(`失败: ${result.message}`);
                }
            } catch (error) {
                console.error(error);
                item.status = 'error';
                addSystemMessage(`网络错误: ${error.message}`);
            }
            renderList();
        }

        progressFill.style.width = "100%";
        progressText.innerText = "全部任务已完成";
        startBtn.disabled = false;
        renderList();
    });

    openFolderBtn.addEventListener('click', () => {
        fetch('/api/open_folder', { method: 'POST' });
    });

    // ==================== 归档库功能 ====================
    const archiveList = document.getElementById('archive-list');
    const archiveCount = document.getElementById('archive-count');
    const archiveSize = document.getElementById('archive-size');
    const refreshBtn = document.getElementById('refresh-archive');
    const openArchiveBtn = document.getElementById('open-archive-folder');

    // 排序和筛选状态
    let currentSort = 'date';
    let archiveData = [];
    let filteredData = [];
    let fuse = null;

    // UI 元素
    const searchInput = document.getElementById('archive-search');
    const artistFilter = document.getElementById('artist-filter');
    const dateFilter = document.getElementById('date-filter');
    const filterResult = document.getElementById('filter-result');

    // 初始化 Fuse.js
    function initFuse(data) {
        fuse = new Fuse(data, {
            keys: ['name', 'artist'],
            threshold: 0.4,
            ignoreLocation: true
        });
    }

    // 搜索事件 (防抖)
    let searchTimeout;
    searchInput.addEventListener('input', () => {
        clearTimeout(searchTimeout);
        searchTimeout = setTimeout(() => {
            applyFilters();
        }, 200);
    });

    // 歌手筛选
    artistFilter.addEventListener('change', applyFilters);

    // 时间筛选
    dateFilter.addEventListener('change', applyFilters);

    // 排序按钮事件
    document.querySelectorAll('.sort-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.sort-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            currentSort = btn.dataset.sort;
            renderArchive();
        });
    });

    // 应用所有筛选
    function applyFilters() {
        const searchQuery = searchInput.value.trim();
        const selectedArtist = artistFilter.value;
        const selectedDate = dateFilter.value;

        // 从原始数据开始
        let result = [...archiveData];

        // 应用搜索
        if (searchQuery && fuse) {
            const searchResults = fuse.search(searchQuery);
            result = searchResults.map(r => r.item);
        }

        // 应用歌手筛选
        if (selectedArtist) {
            result = result.filter(f => f.artist === selectedArtist);
        }

        // 应用时间筛选
        if (selectedDate) {
            const now = new Date();
            result = result.filter(f => {
                const fileDate = new Date(f.modified);
                const diffDays = Math.floor((now - fileDate) / (1000 * 60 * 60 * 24));

                switch (selectedDate) {
                    case 'today': return diffDays === 0;
                    case 'week': return diffDays <= 7;
                    case 'month': return diffDays <= 30;
                    default: return true;
                }
            });
        }

        filteredData = result;

        // 显示筛选结果信息
        if (searchQuery || selectedArtist || selectedDate) {
            filterResult.innerHTML = `找到 <strong>${filteredData.length}</strong> 首`;
        } else {
            filterResult.innerHTML = '';
        }

        renderArchive();
    }

    // 更新歌手下拉列表
    function updateArtistDropdown() {
        const artists = [...new Set(archiveData.map(f => f.artist))].sort((a, b) =>
            a.localeCompare(b, 'zh-CN')
        );

        artistFilter.innerHTML = '<option value="">全部歌手</option>';
        artists.forEach(artist => {
            const count = archiveData.filter(f => f.artist === artist).length;
            const option = document.createElement('option');
            option.value = artist;
            option.textContent = `${artist} (${count})`;
            artistFilter.appendChild(option);
        });
    }

    // 从文件名解析歌手
    function parseArtist(filename) {
        // 尝试匹配 "歌手 - 歌名" 格式
        const match = filename.match(/^(.+?)\s*[-–—]\s*.+\.\w+$/);
        if (match) return match[1].trim();
        return '未知歌手';
    }

    // 解析日期分组 (今天/昨天/本周/更早)
    function getDateGroup(dateStr) {
        const date = new Date(dateStr);
        const now = new Date();
        const diff = Math.floor((now - date) / (1000 * 60 * 60 * 24));

        if (diff === 0) return '今天';
        if (diff === 1) return '昨天';
        if (diff <= 7) return '本周';
        if (diff <= 30) return '本月';
        return '更早';
    }

    async function loadArchive() {
        try {
            const response = await fetch('/api/archive');
            const data = await response.json();

            archiveCount.innerText = data.files.length;
            archiveSize.innerText = formatSize(data.total_size);

            // 添加解析后的歌手信息
            archiveData = data.files.map(file => ({
                ...file,
                artist: parseArtist(file.name),
                dateGroup: getDateGroup(file.modified)
            }));

            // 初始化搜索引擎和下拉列表
            initFuse(archiveData);
            updateArtistDropdown();

            // 初始时显示所有数据
            filteredData = [...archiveData];
            renderArchive();
        } catch (error) {
            console.error('加载归档失败:', error);
        }
    }

    function renderArchive() {
        // 使用筛选后的数据
        const dataToRender = filteredData.length > 0 || searchInput.value || artistFilter.value || dateFilter.value
            ? filteredData
            : archiveData;

        if (dataToRender.length === 0) {
            archiveList.innerHTML = `
                <div class="archive-empty">
                    <span class="ascii-icon">[ ? ]</span>
                    <p>${archiveData.length === 0 ? '归档库为空，请先转换一些文件' : '没有找到匹配的歌曲'}</p>
                </div>
            `;
            return;
        }

        // 排序
        let sorted = [...dataToRender];
        switch (currentSort) {
            case 'date':
                sorted.sort((a, b) => new Date(b.modified) - new Date(a.modified));
                break;
            case 'artist':
                sorted.sort((a, b) => a.artist.localeCompare(b.artist, 'zh-CN'));
                break;
            case 'name':
                sorted.sort((a, b) => a.name.localeCompare(b.name, 'zh-CN'));
                break;
        }

        archiveList.innerHTML = '';

        // 列表视图
        sorted.forEach(file => {
            archiveList.appendChild(createArchiveItem(file));
        });
    }

    function createArchiveItem(file) {
        const div = document.createElement('div');
        div.className = 'archive-item';
        div.dataset.filename = file.name;
        div.innerHTML = `
            <input type="checkbox" class="item-checkbox" data-name="${file.name}">
            <span class="archive-icon" onclick="playAudio('${encodeURIComponent(file.name)}')" title="播放">♫</span>
            <div class="archive-info">
                <div class="archive-name" onclick="openRenameModal('${encodeURIComponent(file.name)}')" title="点击重命名">${file.name}</div>
                <div class="archive-meta">
                    <span>${file.artist}</span>
                    <span>${formatSize(file.size)}</span>
                    <span>${file.modified}</span>
                </div>
            </div>
            <div class="archive-actions-item">
                <button class="archive-btn" onclick="playAudio('${encodeURIComponent(file.name)}')" title="播放">▶</button>
                <button class="archive-btn copy" onclick="copyArchiveFile('${encodeURIComponent(file.name)}')" title="复制">复制</button>
                <button class="archive-btn open" onclick="openArchiveFileLocation('${encodeURIComponent(file.name)}')" title="定位">定位</button>
                <button class="archive-btn delete" onclick="deleteArchiveFile('${encodeURIComponent(file.name)}')" title="删除">删除</button>
            </div>
        `;
        return div;
    }

    function formatSize(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        return (bytes / 1024 / 1024).toFixed(2) + ' MB';
    }

    window.deleteArchiveFile = async function (encodedName) {
        const name = decodeURIComponent(encodedName);
        if (!confirm(`确定要删除 "${name}" 吗？`)) return;

        try {
            const response = await fetch('/api/archive/delete', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ filename: name })
            });
            const result = await response.json();
            if (result.success) {
                loadArchive();
            } else {
                alert('删除失败: ' + result.message);
            }
        } catch (error) {
            alert('删除失败: ' + error.message);
        }
    };

    // 复制文件到剪贴板（整个文件）
    window.copyArchiveFile = async function (encodedName) {
        const name = decodeURIComponent(encodedName);
        try {
            const response = await fetch('/api/archive/copy', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ filename: name })
            });
            const result = await response.json();
            if (result.success) {
                showToast('文件已复制，可直接粘贴');
            } else {
                alert('复制失败: ' + result.message);
            }
        } catch (error) {
            alert('复制失败: ' + error.message);
        }
    };

    // 在文件夹中显示
    window.openArchiveFileLocation = async function (encodedName) {
        const name = decodeURIComponent(encodedName);
        try {
            await fetch('/api/archive/locate', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ filename: name })
            });
        } catch (error) {
            console.error('打开失败:', error);
        }
    };

    // 简单的 Toast 提示
    function showToast(message) {
        const toast = document.createElement('div');
        toast.className = 'toast-message';
        toast.textContent = message;
        document.body.appendChild(toast);
        setTimeout(() => {
            toast.classList.add('show');
        }, 10);
        setTimeout(() => {
            toast.classList.remove('show');
            setTimeout(() => toast.remove(), 300);
        }, 2000);
    }

    refreshBtn.addEventListener('click', loadArchive);
    openArchiveBtn.addEventListener('click', () => {
        fetch('/api/open_folder', { method: 'POST' });
    });

    // ==================== 批量选择功能 ====================
    const selectAllCheckbox = document.getElementById('select-all-files');
    const batchDeleteBtn = document.getElementById('batch-delete');
    const selectedCountEl = document.getElementById('selected-count');
    let selectedFiles = new Set();

    selectAllCheckbox.addEventListener('change', () => {
        const checkboxes = document.querySelectorAll('.item-checkbox');
        checkboxes.forEach(cb => {
            cb.checked = selectAllCheckbox.checked;
            if (selectAllCheckbox.checked) {
                selectedFiles.add(cb.dataset.name);
            }
        });
        if (!selectAllCheckbox.checked) {
            selectedFiles.clear();
        }
        updateSelectionUI();
    });

    archiveList.addEventListener('change', (e) => {
        if (e.target.classList.contains('item-checkbox')) {
            if (e.target.checked) {
                selectedFiles.add(e.target.dataset.name);
            } else {
                selectedFiles.delete(e.target.dataset.name);
            }
            updateSelectionUI();
        }
    });

    function updateSelectionUI() {
        const count = selectedFiles.size;
        batchDeleteBtn.style.display = count > 0 ? '' : 'none';
        selectedCountEl.style.display = count > 0 ? '' : 'none';
        selectedCountEl.querySelector('strong').textContent = count;

        // 更新行选中样式
        document.querySelectorAll('.archive-item').forEach(item => {
            const name = item.dataset.filename;
            item.classList.toggle('selected', selectedFiles.has(name));
        });
    }

    batchDeleteBtn.addEventListener('click', async () => {
        if (selectedFiles.size === 0) return;
        if (!confirm(`确定要删除选中的 ${selectedFiles.size} 个文件吗？`)) return;

        try {
            const response = await fetch('/api/archive/batch_delete', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ filenames: Array.from(selectedFiles) })
            });
            const result = await response.json();
            showToast(result.message);
            selectedFiles.clear();
            selectAllCheckbox.checked = false;
            loadArchive();
        } catch (error) {
            alert('批量删除失败: ' + error.message);
        }
    });

    // ==================== 音频播放器 ====================
    const audioPlayer = document.getElementById('audio-player');
    const audioElement = document.getElementById('audio-element');
    const playerName = document.getElementById('player-name');
    const playerToggle = document.getElementById('player-toggle');
    const playerPrev = document.getElementById('player-prev');
    const playerNext = document.getElementById('player-next');
    const playerClose = document.getElementById('player-close');
    const progressBar = document.getElementById('progress-bar');
    const volumeBar = document.getElementById('volume-bar');
    const timeCurrent = document.getElementById('time-current');
    const timeTotal = document.getElementById('time-total');

    let currentPlayingFile = null;
    let playlist = [];

    window.playAudio = function (encodedName) {
        const name = decodeURIComponent(encodedName);
        currentPlayingFile = name;

        // 更新播放列表
        playlist = filteredData.length > 0 ? filteredData.map(f => f.name) : archiveData.map(f => f.name);

        audioElement.src = '/api/audio/' + encodeURIComponent(name);
        audioElement.play();

        playerName.textContent = name;
        audioPlayer.style.display = 'flex';
        document.body.classList.add('player-visible');
        document.getElementById('play-icon').className = 'icon-pause';
    };

    playerToggle.addEventListener('click', () => {
        const playIcon = document.getElementById('play-icon');
        if (audioElement.paused) {
            audioElement.play();
            playIcon.className = 'icon-pause';
        } else {
            audioElement.pause();
            playIcon.className = 'icon-play';
        }
    });

    playerClose.addEventListener('click', () => {
        audioElement.pause();
        audioElement.src = '';
        audioPlayer.style.display = 'none';
        document.body.classList.remove('player-visible');
        currentPlayingFile = null;
    });

    playerPrev.addEventListener('click', () => {
        const idx = playlist.indexOf(currentPlayingFile);
        if (idx > 0) {
            window.playAudio(encodeURIComponent(playlist[idx - 1]));
        }
    });

    playerNext.addEventListener('click', () => {
        const idx = playlist.indexOf(currentPlayingFile);
        if (idx < playlist.length - 1) {
            window.playAudio(encodeURIComponent(playlist[idx + 1]));
        }
    });

    audioElement.addEventListener('timeupdate', () => {
        if (audioElement.duration) {
            progressBar.value = (audioElement.currentTime / audioElement.duration) * 100;
            timeCurrent.textContent = formatTime(audioElement.currentTime);
        }
    });

    audioElement.addEventListener('loadedmetadata', () => {
        timeTotal.textContent = formatTime(audioElement.duration);
    });

    audioElement.addEventListener('ended', () => {
        // 自动播放下一首
        const idx = playlist.indexOf(currentPlayingFile);
        if (idx < playlist.length - 1) {
            window.playAudio(encodeURIComponent(playlist[idx + 1]));
        } else {
            document.getElementById('play-icon').className = 'icon-play';
        }
    });

    progressBar.addEventListener('input', () => {
        if (audioElement.duration) {
            audioElement.currentTime = (progressBar.value / 100) * audioElement.duration;
        }
    });

    volumeBar.addEventListener('input', () => {
        audioElement.volume = volumeBar.value / 100;
        localStorage.setItem('kugou-player-volume', volumeBar.value);
    });

    // 加载保存的音量
    const savedVolume = localStorage.getItem('kugou-player-volume');
    if (savedVolume !== null) {
        volumeBar.value = savedVolume;
        audioElement.volume = savedVolume / 100;
    } else {
        audioElement.volume = 0.8;
    }

    function formatTime(seconds) {
        const m = Math.floor(seconds / 60);
        const s = Math.floor(seconds % 60);
        return `${m}:${s.toString().padStart(2, '0')}`;
    }

    // ==================== 重命名功能 ====================
    const renameModal = document.getElementById('rename-modal');
    const renameInput = document.getElementById('rename-input');
    const renameConfirm = document.getElementById('rename-confirm');
    const renameCancel = document.getElementById('rename-cancel');
    const renameClose = document.getElementById('rename-close');
    let renamingFile = null;

    window.openRenameModal = function (encodedName) {
        const name = decodeURIComponent(encodedName);
        renamingFile = name;
        const baseName = name.replace(/\.[^/.]+$/, '');
        renameInput.value = baseName;
        renameModal.style.display = 'flex';
        renameInput.focus();
        renameInput.select();
    };

    function closeRenameModal() {
        renameModal.style.display = 'none';
        renamingFile = null;
    }

    renameCancel.addEventListener('click', closeRenameModal);
    renameClose.addEventListener('click', closeRenameModal);

    renameConfirm.addEventListener('click', async () => {
        const newName = renameInput.value.trim();
        if (!newName) {
            alert('请输入新文件名');
            return;
        }

        try {
            const response = await fetch('/api/archive/rename', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ old_name: renamingFile, new_name: newName })
            });
            const result = await response.json();
            if (result.success) {
                showToast('重命名成功');
                closeRenameModal();
                loadArchive();
            } else {
                alert('重命名失败: ' + result.message);
            }
        } catch (error) {
            alert('重命名失败: ' + error.message);
        }
    });

    renameInput.addEventListener('keypress', (e) => {
        if (e.key === 'Enter') {
            renameConfirm.click();
        }
    });

    // ==================== 快捷键支持 ====================
    document.addEventListener('keydown', (e) => {
        // Delete 键删除选中文件
        if (e.key === 'Delete' && selectedFiles.size > 0) {
            batchDeleteBtn.click();
        }

        // Escape 关闭模态框
        if (e.key === 'Escape') {
            if (renameModal.style.display !== 'none') {
                closeRenameModal();
            }
        }

        // 空格键播放/暂停
        if (e.key === ' ' && audioPlayer.style.display !== 'none' && e.target.tagName !== 'INPUT') {
            e.preventDefault();
            playerToggle.click();
        }
    });

    // Ctrl+V 粘贴文件（在转换页面）
    document.addEventListener('paste', async (e) => {
        if (document.getElementById('page-convert').style.display !== 'none') {
            const items = e.clipboardData?.items;
            if (items) {
                for (const item of items) {
                    if (item.kind === 'file') {
                        const file = item.getAsFile();
                        if (file) {
                            addFileToQueue(file);
                        }
                    }
                }
            }
        }
    });

    // 将文件添加到队列的函数
    function addFileToQueue(file) {
        const allowed = ['.kgg', '.kgm', '.kgma'];
        const ext = '.' + file.name.split('.').pop().toLowerCase();
        if (!allowed.includes(ext)) {
            showToast('不支持的文件格式: ' + ext);
            return;
        }
        if (!filesQueue.some(f => f.name === file.name)) {
            filesQueue.push(file);
            renderFileList();
        }
    }

    // ==================== 设置页面功能 ====================
    const outputPathInput = document.getElementById('output-path');
    const customPathInput = document.getElementById('custom-path');
    const applyPathBtn = document.getElementById('apply-path');
    const browseBtn = document.getElementById('browse-folder');
    const pathStatus = document.getElementById('path-status');

    async function loadSettings() {
        try {
            const response = await fetch('/api/settings');
            const data = await response.json();
            outputPathInput.value = data.output_folder;
        } catch (error) {
            console.error('加载设置失败:', error);
        }
    }

    // 切换到设置页时加载
    navTabs.forEach(tab => {
        tab.addEventListener('click', () => {
            if (tab.dataset.page === 'settings') {
                loadSettings();
            }
        });
    });

    applyPathBtn.addEventListener('click', async () => {
        const newPath = customPathInput.value.trim();
        if (!newPath) {
            showPathStatus('请输入有效路径', 'error');
            return;
        }

        try {
            const response = await fetch('/api/settings/output_folder', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ path: newPath })
            });
            const result = await response.json();

            if (result.success) {
                outputPathInput.value = result.path;
                customPathInput.value = '';
                showPathStatus('输出目录已更新', 'success');
            } else {
                showPathStatus(result.message, 'error');
            }
        } catch (error) {
            showPathStatus('设置失败: ' + error.message, 'error');
        }
    });

    browseBtn.addEventListener('click', () => {
        fetch('/api/open_folder', { method: 'POST' });
    });

    function showPathStatus(message, type) {
        pathStatus.innerText = message;
        pathStatus.className = 'settings-hint ' + type;
        setTimeout(() => {
            pathStatus.innerText = '';
            pathStatus.className = 'settings-hint';
        }, 3000);
    }

    // ==================== 主题切换 ====================
    const themeGrid = document.getElementById('theme-grid');
    const themeCards = themeGrid.querySelectorAll('.theme-card');

    // 加载保存的主题
    const savedTheme = localStorage.getItem('kugou-decoder-theme') || 'tokyo-night';
    document.documentElement.setAttribute('data-theme', savedTheme);
    themeCards.forEach(card => {
        card.classList.toggle('active', card.dataset.theme === savedTheme);
    });

    // 主题切换事件
    themeCards.forEach(card => {
        card.addEventListener('click', () => {
            const theme = card.dataset.theme;

            // 更新 UI
            themeCards.forEach(c => c.classList.remove('active'));
            card.classList.add('active');

            // 应用主题
            document.documentElement.setAttribute('data-theme', theme);

            // 保存到 localStorage
            localStorage.setItem('kugou-decoder-theme', theme);

            // 添加过渡动画
            document.body.style.transition = 'background-color 0.3s, color 0.3s';
            setTimeout(() => {
                document.body.style.transition = '';
            }, 300);
        });
    });

    // ==================== 自动监控功能 ====================
    const watchEnabled = document.getElementById('watch-enabled');
    const watchFolder = document.getElementById('watch-folder');
    const applyWatchBtn = document.getElementById('apply-watch');
    const scanNowBtn = document.getElementById('scan-now');
    const clearHistoryBtn = document.getElementById('clear-history');
    const watchStatus = document.getElementById('watch-status');
    const logList = document.getElementById('log-list');

    // 加载监控配置
    async function loadWatchConfig() {
        try {
            const response = await fetch('/api/watch/config');
            const data = await response.json();

            watchEnabled.checked = data.enabled;
            watchFolder.value = data.folder || '';

            renderLog(data.log || []);
        } catch (error) {
            console.error('加载监控配置失败:', error);
        }
    }

    // 渲染处理日志
    function renderLog(logs) {
        if (!logs || logs.length === 0) {
            logList.innerHTML = '<p class="log-empty">暂无处理记录</p>';
            return;
        }

        logList.innerHTML = logs.map(log => {
            let status = 'failed';
            let icon = '✗';
            if (log.success) {
                status = 'success';
                icon = '✓';
            } else if (log.skipped) {
                status = 'skipped';
                icon = '⊘';
            }

            return `
                <div class="log-item ${status}">
                    <span class="log-icon">${icon}</span>
                    <span class="log-name" title="${log.name}">${log.name}</span>
                    <span class="log-time">${log.time}</span>
                </div>
            `;
        }).join('');
    }

    // 保存监控配置
    async function saveWatchConfig() {
        try {
            const response = await fetch('/api/watch/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    enabled: watchEnabled.checked,
                    folder: watchFolder.value.trim()
                })
            });
            const result = await response.json();

            if (result.success) {
                showWatchStatus('配置已保存', 'success');
            } else {
                showWatchStatus(result.message, 'error');
            }
        } catch (error) {
            showWatchStatus('保存失败: ' + error.message, 'error');
        }
    }

    // 立即扫描
    async function scanNow() {
        scanNowBtn.disabled = true;
        scanNowBtn.textContent = '扫描中...';

        try {
            const response = await fetch('/api/watch/scan', {
                method: 'POST'
            });
            const result = await response.json();

            showWatchStatus(result.message, result.success ? 'success' : 'error');

            // 刷新日志
            loadWatchConfig();

            // 刷新归档列表
            loadArchive();
        } catch (error) {
            showWatchStatus('扫描失败: ' + error.message, 'error');
        } finally {
            scanNowBtn.disabled = false;
            scanNowBtn.textContent = '立即扫描';
        }
    }

    // 清除历史
    async function clearHistory() {
        if (!confirm('确定要清除所有处理记录吗？这将允许重新处理已处理过的文件。')) return;

        try {
            await fetch('/api/watch/clear_history', { method: 'POST' });
            showWatchStatus('处理记录已清除', 'success');
            loadWatchConfig();
        } catch (error) {
            showWatchStatus('操作失败', 'error');
        }
    }

    function showWatchStatus(message, type) {
        watchStatus.innerText = message;
        watchStatus.className = 'settings-hint ' + type;
        setTimeout(() => {
            watchStatus.innerText = '';
            watchStatus.className = 'settings-hint';
        }, 3000);
    }

    // 绑定事件
    watchEnabled.addEventListener('change', saveWatchConfig);
    applyWatchBtn.addEventListener('click', saveWatchConfig);
    scanNowBtn.addEventListener('click', scanNow);
    clearHistoryBtn.addEventListener('click', clearHistory);

    // 初始加载
    loadWatchConfig();
});
