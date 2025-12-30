const { app, BrowserWindow, shell, dialog } = require('electron');
const path = require('path');
const { spawn, execSync } = require('child_process');
const fs = require('fs');

let mainWindow;
let flaskProcess = null;
let flaskPid = null;

const FLASK_URL = 'http://127.0.0.1:5000';

// 日志
function log(msg) {
    console.log(`[${new Date().toISOString()}] ${msg}`);
}

// 获取 Flask 路径
function getFlaskPath() {
    if (app.isPackaged) {
        return path.join(process.resourcesPath, 'flask', 'KugouDecoder.exe');
    }
    return null;
}

// 强制终止进程及其子进程
function killFlaskProcess() {
    if (flaskPid) {
        log(`终止Flask进程树 (PID: ${flaskPid})`);
        try {
            // Windows: 使用 taskkill 强制终止进程树
            execSync(`taskkill /PID ${flaskPid} /T /F`, { stdio: 'ignore' });
            log('Flask进程已终止');
        } catch (e) {
            log(`终止进程失败: ${e.message}`);
        }
        flaskPid = null;
    }

    if (flaskProcess) {
        try {
            flaskProcess.kill('SIGKILL');
        } catch (e) { }
        flaskProcess = null;
    }
}

// 启动 Flask
function startFlask() {
    const flaskPath = getFlaskPath();
    if (!flaskPath) {
        log('开发模式，跳过Flask启动');
        return;
    }

    log(`Flask路径: ${flaskPath}`);

    if (!fs.existsSync(flaskPath)) {
        log('Flask文件不存在!');
        dialog.showErrorBox('错误', `找不到Flask: ${flaskPath}`);
        return;
    }

    try {
        flaskProcess = spawn(flaskPath, [], {
            cwd: path.dirname(flaskPath),
            stdio: 'pipe',
            windowsHide: true,
            detached: false
        });

        flaskPid = flaskProcess.pid;
        log(`Flask进程已启动 (PID: ${flaskPid})`);

        flaskProcess.stdout.on('data', d => log(`Flask: ${d}`));
        flaskProcess.stderr.on('data', d => log(`Flask err: ${d}`));
        flaskProcess.on('error', e => log(`Flask error: ${e.message}`));
        flaskProcess.on('exit', (code) => {
            log(`Flask exit: ${code}`);
            flaskProcess = null;
            flaskPid = null;
        });

    } catch (e) {
        log(`启动Flask失败: ${e.message}`);
    }
}

// 创建窗口
function createWindow() {
    log('创建窗口');

    mainWindow = new BrowserWindow({
        width: 1280,
        height: 800,
        minWidth: 1000,
        minHeight: 600,
        title: '酷狗音频解码终端',
        backgroundColor: '#1a1b26',
        show: true,
        frame: false,
        titleBarStyle: 'hidden',
        titleBarOverlay: {
            color: '#1a1b26',
            symbolColor: '#7aa2f7',
            height: 40
        },
        webPreferences: {
            nodeIntegration: false,
            contextIsolation: true
        }
    });

    // 先显示加载页面
    mainWindow.loadURL(`data:text/html;charset=utf-8,
        <html>
        <head><style>
            body { 
                background: #1a1b26; 
                color: #7aa2f7; 
                font-family: sans-serif;
                display: flex;
                justify-content: center;
                align-items: center;
                height: 100vh;
                margin: 0;
            }
            .loader { text-align: center; }
            h1 { font-size: 24px; }
            p { color: #565f89; }
        </style></head>
        <body>
            <div class="loader">
                <h1>酷狗音频解码终端</h1>
                <p>正在启动服务，请稍候...</p>
            </div>
        </body>
        </html>
    `);

    // 等待Flask启动后加载
    setTimeout(() => {
        log('尝试连接Flask...');
        checkAndLoad();
    }, 2000);

    mainWindow.on('closed', () => {
        mainWindow = null;
    });
}

// 检查Flask并加载
function checkAndLoad(retries = 20) {
    const http = require('http');

    http.get(FLASK_URL, (res) => {
        log('Flask已就绪，加载主页');
        if (mainWindow) {
            mainWindow.loadURL(FLASK_URL);
        }
    }).on('error', () => {
        if (retries > 0) {
            log(`等待Flask... 剩余${retries}次`);
            setTimeout(() => checkAndLoad(retries - 1), 500);
        } else {
            log('Flask连接失败');
            if (mainWindow) {
                mainWindow.loadURL(`data:text/html;charset=utf-8,
                    <html>
                    <head><style>
                        body { 
                            background: #1a1b26; 
                            color: #f7768e; 
                            font-family: sans-serif;
                            display: flex;
                            justify-content: center;
                            align-items: center;
                            height: 100vh;
                            margin: 0;
                        }
                    </style></head>
                    <body>
                        <div>
                            <h1>启动失败</h1>
                            <p>无法连接到后端服务</p>
                            <p>请检查是否有其他程序占用5000端口</p>
                        </div>
                    </body>
                    </html>
                `);
            }
        }
    });
}

// 单实例锁
const gotLock = app.requestSingleInstanceLock();
if (!gotLock) {
    log('已有实例运行，退出');
    app.quit();
} else {
    app.on('second-instance', () => {
        if (mainWindow) {
            if (mainWindow.isMinimized()) mainWindow.restore();
            mainWindow.focus();
        }
    });

    app.whenReady().then(() => {
        log('App ready');
        log(`isPackaged: ${app.isPackaged}`);
        log(`resourcesPath: ${process.resourcesPath}`);

        startFlask();
        createWindow();
    });
}

// 所有窗口关闭时
app.on('window-all-closed', () => {
    log('所有窗口已关闭');
    killFlaskProcess();
    if (process.platform !== 'darwin') {
        app.quit();
    }
});

// 退出前清理
app.on('before-quit', () => {
    log('应用即将退出');
    killFlaskProcess();
});

// 应用退出时确保清理
app.on('will-quit', () => {
    log('应用退出');
    killFlaskProcess();
});

// 处理未捕获异常
process.on('uncaughtException', (err) => {
    log(`未捕获异常: ${err.message}`);
    killFlaskProcess();
});

// SIGINT 处理 (Ctrl+C)
process.on('SIGINT', () => {
    log('收到 SIGINT');
    killFlaskProcess();
    app.quit();
});
