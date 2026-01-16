# MagnetDownload GUI 界面原型图

## 🎨 界面设计原型

本文档包含了 MagnetDownload 应用程序的完整 SVG 界面原型图。所有设计都采用现代化的深色主题，提供直观的用户体验。

### 1. 主界面设计 (1200x800)

![主界面](svg/main_window.svg)

**主要特性:**
- 清晰的功能分区布局
- 实时下载进度显示
- 多任务管理界面
- 详细信息面板
- 全局状态监控

### 2. 磁力链接解析对话框 (600x500)

![磁力链接解析](svg/magnet_resolver_dialog.svg)

**核心功能:**
- 实时解析进度显示
- 网络状态监控 (Peers, DHT, Trackers)
- 详细的解析日志
- 可取消的解析过程
- 动画加载指示器

### 3. 文件选择对话框 (900x700)

![文件选择](svg/file_selector_dialog.svg)

**主要功能:**
- 完整的种子信息展示
- 文件列表与优先级设置
- 保存位置配置
- 下载选项设置 (连接、速度、优先级)
- 选择统计与预估时间
- 批量操作支持

### 4. 下载详情对话框 (1000x800)

![下载详情](svg/download_details_dialog.svg)

**详细信息:**
- 多标签页界面 (常规、文件、Peers、统计、选项、日志)
- 实时进度概览
- 文件级别的进度显示
- 分片地图可视化
- 实时速度图表
- 完整的下载统计

### 5. Peers 连接视图 (1000x600)

![Peers视图](svg/peers_view.svg)

**连接管理:**
- 连接统计概览
- 详细的 Peer 列表
- 连接状态指示 (已连接/连接中/失败)
- 地理位置分布
- Peer 操作功能 (断开、黑名单)
- 实时速度监控

### 6. 设置对话框 (800x600)

![设置界面](svg/settings_dialog.svg)

**配置选项:**
- 分类导航设计
- 连接设置 (端口、UPnP、NAT-PMP)
- 连接限制配置
- 代理服务器设置
- 多页面设置管理

## 🎨 视觉设计元素

### 图标设计规范

```
文件类型图标:
📁 文件夹 (通用)
📀 ISO镜像文件  
🎬 视频文件 (.mp4, .mkv, .avi)
🎵 音频文件 (.mp3, .flac, .wav)
📦 压缩文件 (.zip, .rar, .7z)
📄 文档文件 (.pdf, .doc, .txt)
🖼️ 图片文件 (.jpg, .png, .gif)
⚙️ 程序文件 (.exe, .deb, .dmg)

状态图标:
🟢 正常/活跃状态
🟡 警告/等待状态  
🔴 错误/失败状态
⏸️ 暂停状态
▶️ 播放/恢复状态
✅ 完成状态
❌ 取消/禁用状态

网络图标:
🌍 互联网连接
🏠 本地网络连接
📡 DHT网络
🔍 搜索/查找
📤 上传
📥 下载
```

### 颜色主题方案

#### 深色主题 (Dark Theme)
```css
Primary Colors:
- Background: #2C3E50 (深蓝灰)
- Surface: #34495E (中蓝灰)  
- Primary: #3498DB (蓝色)
- Secondary: #95A5A6 (灰色)

Status Colors:
- Success: #27AE60 (绿色)
- Warning: #F39C12 (橙色)
- Error: #E74C3C (红色)
- Info: #3498DB (蓝色)

Text Colors:
- Primary Text: #ECF0F1 (浅灰白)
- Secondary Text: #BDC3C7 (中灰)
- Disabled Text: #7F8C8D (深灰)
```

#### 浅色主题 (Light Theme)
```css
Primary Colors:
- Background: #FFFFFF (白色)
- Surface: #F8F9FA (浅灰)
- Primary: #007BFF (蓝色)
- Secondary: #6C757D (灰色)

Status Colors:
- Success: #28A745 (绿色)
- Warning: #FFC107 (黄色)
- Error: #DC3545 (红色)
- Info: #17A2B8 (青色)

Text Colors:
- Primary Text: #212529 (深灰黑)
- Secondary Text: #6C757D (中灰)
- Disabled Text: #ADB5BD (浅灰)
```

## 📐 布局规范

### 间距系统
```
Extra Small: 4px   - 组件内部间距
Small: 8px         - 相关元素间距
Medium: 16px       - 组件间距
Large: 24px        - 区块间距
Extra Large: 32px  - 页面边距
```

### 字体规范
```
标题字体:
- H1: 24px, Bold
- H2: 20px, Bold  
- H3: 18px, Bold
- H4: 16px, Bold

正文字体:
- Body Large: 16px, Regular
- Body Medium: 14px, Regular
- Body Small: 12px, Regular
- Caption: 11px, Regular

等宽字体:
- Code: 14px, Monospace (用于Hash、URL等)
```

### 组件尺寸
```
按钮:
- Large: 48px height
- Medium: 40px height  
- Small: 32px height

输入框:
- Height: 40px
- Border Radius: 4px

表格行:
- Height: 48px
- Header Height: 56px

对话框:
- Min Width: 400px
- Max Width: 800px
- Border Radius: 8px
```

这个GUI设计文档提供了完整的界面原型和设计规范。你可以看到：

1. **主界面布局** - 清晰的功能分区
2. **对话框设计** - 用户友好的交互流程  
3. **视觉设计规范** - 统一的颜色和字体系统
4. **响应式设计** - 适配不同屏幕尺寸

接下来我们可以选择一个组件开始实现，比如从磁力链接解析对话框开始？