#include "Editor.hpp"

#include <algorithm>
#include <cctype>
#include <exception>

//以下提示均可用于Basic部分的实现,部分函数在Advanced部分中需要修改
//在该文件中,有些函数我们完整保留了正确的实现,有些函数去掉了一些,其余的完全需要你自己填写
namespace sjtu {

namespace {
//匿名namespace,提供了只给当前文件使用的辅助函数

std::string Trim(std::string value) {
    //去掉字符串两端的空白,保留中间的内容;全部是空白时返回空字符串
    //你可以分别从两端找到第一个非空白字符,注意反向迭代器转回正向迭代器时的边界
    // 1. 从左向右扫描，找到第一个“非空白”字符
    auto left = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {return std::isspace(c);});

    // 如果整个字符串全是空白字符（left 一路走到了结尾）
    if (left == value.end()) {
        return "";
    }

    // 2. 从右向左扫描，找到倒数第一个“非空白”字符
    auto right_rev = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {return std::isspace (c);});

    // 3. 截取中间有效部分并返回
    // right_rev 是反向迭代器，调用 .base() 会把它转换回正向迭代器。
    // 神奇的边界机制：转换后的正向迭代器刚好指向 right_rev 对应字符的“下一个”位置，
    // 完美符合 C++ 迭代器 [开始, 结束) “左闭右开”的区间要求！
    return std::string(left, right_rev.base());
}

//判断是否为ASCII可打印字符,Tab由插入模式另外处理
bool IsPrintable(char value) { return value >= 0x20U && value < 0x7FU; }
    
} // namespace

//用path初始化文件缓冲区,Terminal构造时会准备好终端输入环境
Editor::Editor(const std::filesystem::path& path) : buffer_(path), terminal_() {}

void Editor::Run() {
    //每轮先刷新画面,再读取并处理一个按键;退出循环后清屏
    while (running_) {
        RefreshScreen();
        ProcessKey(terminal_.ReadKey());
    }
    terminal_.ClearScreen();
}

//返回编辑器是否还需要继续运行
bool Editor::IsRunning() const noexcept { return false; }

void Editor::RefreshScreen() {
    // 1. 获取终端大小，更新窗口大小并确保光标可见
    ScreenSize screen_size = terminal_.GetScreenSize();
    window_.Resize(screen_size);
    window_.EnsureCursorVisible(buffer_);

    // 2. 把当前模式、命令和提示打包成 RenderState
    RenderState state = {
        .mode_ = mode_,
        .command_ = command_,
        .message_ = message_
    };

    // 3. 让 Renderer 生成一帧字符串，再交给 Terminal 输出
    std::string frame = renderer_.Render(buffer_, window_, state);
    terminal_.WriteOutput(frame);
}

void Editor::ProcessKey(KeyEvent key) {
    // 1. (可选) 先处理所有模式都能使用的 Ctrl-Q，用于紧急退出
    // 检查是否按下了 Ctrl+Q (或 ctrl+q)
    if (key.IsControl('q') || key.IsControl('Q')) {
        running_ = false;
        return;
    }

    // 2. 按当前模式分发给命令行或插入模式的处理函数
    switch (mode_) {
        case Mode::Insert:
            HandleInsert(key);
            return;

        case Mode::CommandLine:
            HandleCommandLine(key);
            return;

        case Mode::Normal:
            break;

        default:
            break;
    }

    // 3. Normal 模式下清除旧提示，将按键交给 parser，再执行生成的 Action
    // 如果底部状态栏有错误或旧消息，可在每次有效输入前清除
    message_.clear(); 

    // 直接接收动作
    EditorAction action = normal_parser_.Feed(key);
    Execute(action);
}

void Editor::Execute(const EditorAction& action) {
    // 根据 Action 的种类调用对应模块
    switch (action.kind_) {
        case ActionKind::None:
            return;

        case ActionKind::Move:
            // 交给 Window 对象的 ApplyMotion 来处理移动
            window_.ApplyMotion(buffer_, action.motion_.value()); 
            return;

        case ActionKind::InsertBefore:
            // 进入 InsertMode，位置是当前光标位置
            // 插入模式下通常允许光标停在行末之后，所以 allow_line_end 传 true
            EnterInsert(window_.GetCursor());
            return;

        case ActionKind::InsertAfter: {
            // 从当前字符之后进入 InsertMode
            auto pos = window_.GetCursor();
            
            // 特判：如果当前行是空的，直接在当前位置进入
            // 否则将列号向后移一位
            const auto& line = buffer_.GetLineAt(pos.row_);
            if (!line.empty() && pos.column_ < line.size()) {
                pos.column_ += 1;
            }
            
            EnterInsert(pos);
            return;
        }

        case ActionKind::EnterCommandLine:
            // 进入命令行模式，并清空历史状态
            message_.clear();
            command_.clear();
            mode_ = Mode::CommandLine;
            return;
    }
}

//Insert模式下Editor对于KeyEvent的处理.
void Editor::HandleInsert(KeyEvent key) {
    // 获取当前光标位置
    Position pos = window_.GetCursor(); //

    // 1. 若是 Escape 退出插入模式 (ASCII 27)
    if (key.code_ == KeyCode::Escape){
        LeaveInsert();
        return;
    }

    // 2. Enter 在光标处分行，光标移动到新行开头
    if (key.code_ == KeyCode::Enter){
        buffer_.SplitLine(pos.row_, pos.column_); 
        pos.row_ += 1;
        pos.column_ = 0;
        
        window_.SetCursor(buffer_, pos, true); // 插入模式允许光标位于 line.size()，传入 true
        return;
    }

    // 3. Backspace 删除前一个字符；若在行首且不是第一行，则与上一行合并
    // (终端下退格键通常是 127 DEL 或 8 BS)
    if (key.code_ == KeyCode::Backspace){
        if (pos.column_ > 0) {
            // 光标不在行首，左移后删除字符
            pos.column_ -= 1;
            buffer_.EraseCharacter(pos.row_, pos.column_);
            window_.SetCursor(buffer_, pos, true); //
        } else if (pos.row_ > 0) {
            // 光标在行首且不是第一行，合并到上一行末尾
            auto prev_line_length = buffer_.GetLineAt(pos.row_ - 1).size();
            buffer_.JoinLine(pos.row_ - 1);
            pos.row_ -= 1;
            pos.column_ = prev_line_length;
            window_.SetCursor(buffer_, pos, true); //
        }
        return;
    }

   // 4. 可打印字符和 Tab 插入当前位置，光标向后移动一列
    // 首先判断按键种类是不是普通字符
    if (key.code_ == KeyCode::Character) { 
        // 取出具体的字符值，判断是否可打印或是 Tab
        if (IsPrintable(key.value_) || key.value_ == '\t') {
            buffer_.InsertCharacter(pos.row_, pos.column_, key.value_);
            pos.column_ += 1;
            window_.SetCursor(buffer_, pos, true);
            return;
        }
    }
}
void Editor::EnterInsert(Position position) {
    // 切换到 Insert 模式
    mode_ = Mode::Insert;
    // 清除旧提示和可能的命令行残留
    message_.clear();
    command_.clear();
    // 设置插入位置，并明确允许光标停在行尾字符之后 (allow_line_end = true)
    window_.SetCursor(buffer_, position, true);
}

void Editor::LeaveInsert() {
    // 切换回 Normal 模式
    mode_ = Mode::Normal;
    // 获取当前光标位置
    Position pos = window_.GetCursor();
    // 从插入位置回到 Normal 模式的字符位置：不在行首时先左移一列
    if (pos.column_ > 0) {
        pos.column_ -= 1;
    }
    // 限制光标范围，不允许越过行尾有效字符 (allow_line_end = false)
    window_.SetCursor(buffer_, pos, false);
}

void Editor::HandleCommandLine(KeyEvent key) {
    // 1. Escape (ASCII 27) 取消命令
    if (key.code_ == KeyCode::Escape){
        LeaveCommandLine();
        return;
    }

    // 2. Enter ('\r' 或 '\n') 执行命令
    if (key.code_ == KeyCode::Enter){
        ExecuteCommandLine();
        return;
    }

    // 3. Backspace/Delete (127 或 '\b') 删除末尾字符
    if (key.code_ == KeyCode::Backspace){
        if (!command_.empty()) {
            command_.pop_back(); 
        } else {
            // 如果命令已经被删光了，再次按退格会退出命令行模式
            LeaveCommandLine();
        }
        return;
    }

    // 4. 可打印字符追加到末尾
    if (key.code_ == KeyCode::Character) {
        if (IsPrintable(key.value_)) {
            command_.push_back(key.value_); // 追加的时候用 key.value_
            return;
        }
    }
}

void Editor::ExecuteCommandLine() {
    // 1. 保存去掉首尾空白后的命令
    std::string cmd = command_;
    auto start = cmd.find_first_not_of(" \t");
    if (start == std::string::npos) {
        // 如果全是空白字符，也就是空命令，直接退出并返回
        LeaveCommandLine();
        return;
    }
    auto end = cmd.find_last_not_of(" \t");
    cmd = cmd.substr(start, end - start + 1);

    // 退出命令行模式（此时会清空原有的 command_）
    LeaveCommandLine();

    // 2. 按第一个空格或 Tab 拆成命令名和参数
    auto space_pos = cmd.find_first_of(" \t");
    std::string cmd_name = cmd.substr(0, space_pos);
    // string::substr 如果起始位置超过长度会抛出异常，所以要判断是否找到了空格
    std::string cmd_args = (space_pos != std::string::npos) ? cmd.substr(space_pos + 1) : "";

    // 去掉参数前导的可能多余的空格（可选，但推荐）
    if (!cmd_args.empty()) {
        auto arg_start = cmd_args.find_first_not_of(" \t");
        if (arg_start != std::string::npos) {
            cmd_args = cmd_args.substr(arg_start);
        } else {
            cmd_args.clear();
        }
    }
    // 3. 根据命令名执行对应的动作 (Basic 部分只看命令名)
    if (cmd_name == "q") {
        // 正常退出（如果是完整 Vim 还需检查是否保存，基础版直接退出即可）
        running_ = false;
    } else if (cmd_name == "q!") {
        // 强制退出
        running_ = false;
    } else if (cmd_name == "w") {
        // 如果带了参数，也就是 cmd_args 不为空，就当作 path 传进去
        SaveBuffer(cmd_args);
    } else if (cmd_name == "wq") {
        // 如果保存成功，才退出程序
        if (SaveBuffer(cmd_args)) {
            running_ = false;
        }
    } else {
        // 4. 无法识别的命令写入 message_，供下一次刷新显示
        message_ = "Not an editor command: " + cmd_name;
    }
}
void Editor::LeaveCommandLine() {
    // 恢复 Normal 模式
    mode_ = Mode::Normal;
    
    // 清空正在输入的命令缓冲区
    command_.clear();
}

bool Editor::SaveBuffer(const std::filesystem::path& path) {
    try {
        // 1. path为空时调用Save,否则调用SaveAs
        if (path.empty()) {
            buffer_.Save();
        } else {
            buffer_.SaveAs(path);
        }

        // 3. 成功后生成包含文件名和行数的提示,返回true
        // 利用 Buffer.hpp 提供的 GetDisplayName() 和 GetLineCount() 组合提示信息
        message_ = "\"" + buffer_.GetDisplayName() + "\" " +  std::to_string(buffer_.GetLineCount()) + "L written";
        return true;
        
    } catch (const std::exception& e) {
        // 2. 捕获保存时的异常,把错误写入message_并返回false
        message_ = std::string("E212: Can't open file for writing: ") + e.what();
        return false;
    }
}

} // namespace sjtu
