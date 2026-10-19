#include "Renderer.hpp"
#include "TextLayout.hpp"

namespace sjtu {

//我们在Render中保留了所有需要用到控制序列的部分逻辑,你不应该修改它们.
//啥是控制序列?它们是嵌入在文本流中的“指令”,告诉终端执行特定操作(比如移动光标、改变文字颜色、清屏或清除行内容).   
namespace {

void AppendClearedLine(std::string& frame, std::string_view contents, std::size_t width, bool newline) {
    //追加不超过width列的显示内容,再用ESC[K清除这一行余下的旧内容
    //需要换行时同时输出回车和换行,因为raw模式关闭了终端的自动输出转换
    //你在生成帧的时候生成每一行都应该调用这个函数
    frame.append(contents.substr(0, width));
    frame += "\x1b[K";
    if (newline) {
        frame += "\r\n";
    }
}

std::string CursorSequence(std::size_t row, std::size_t column) {
    //生成定位光标的ANSI转义序列,这里接收的行列都从1开始
    return "\x1b[" + std::to_string(row) + ';' + std::to_string(column) + 'H';
}

}

std::string Renderer::Render(const Buffer& buffer, const Window& window, const RenderState& state) const {
    //1. 隐藏光标并回到屏幕左上角,开始拼接新的一帧(一帧就是一个长string)
    //2. 按视口逐行取Buffer内容,展开Tab后再横向截取;超出文件的行显示~
    //3. 绘制底部一行,优先显示命令行,其次是提示信息,最后是Insert模式标记
    //4. 根据当前模式计算光标的屏幕位置,追加定位序列并重新显示光标
    //你应该区分我们的Cursor光标(它代表当前用户在修改文件的哪个位置)和终端显示的白色亮条,白色亮条在屏幕中的位置是由光标位置和视口位置计算的
    auto& viewport = window.GetViewport();
    auto width = std::max<std::size_t>(viewport.columns_, 1);

    std::string frame;
    frame.reserve((viewport.rows_ + 1) * (width + 8));
    frame += "\x1b[?25l";//隐藏终端光标
    frame += "\x1b[H";//把光标瞬间移动到屏幕的最左上角

    for (std::size_t screen_row = 0; screen_row < viewport.rows_; ++screen_row) {
       //这里是提示2中的部分
       std::size_t buffer_row = viewport.top_ + screen_row;
       if (buffer_row < buffer.GetLineCount()){
           std::string_view line = buffer.GetLineAt(buffer_row);
           std::string expanded_line = ExpandForDisplay(line);
           std::string_view visible_part;
           if (viewport.left_ < expanded_line.size()) {
                visible_part = std::string_view(expanded_line).substr(viewport.left_);
            }
            // 拼接到最终的 frame 里，true 表示要换行
           AppendClearedLine(frame, visible_part, width, true);
        } 
        else
        {
            // 如果超出了文件的总行数，像 Vim 一样显示一个 '~'
            AppendClearedLine(frame, "~", width, true);
        }
       
    }

    std::string bottom;
    if (state.mode_ == Mode::CommandLine) {
        bottom = ":" + state.command_;
    } else if (!state.message_.empty()) {
        bottom = state.message_;
    } else if (state.mode_ == Mode::Insert) {
        bottom = "-- INSERT --";
    }
    AppendClearedLine(frame, bottom, width, false);
    //这里是提示3中的部分
    //我们只会在CommandMode的时候检查一下底部的命令内容,在NormalMode不会看底部,所以message你可以随意写

    // 获取当前光标在 Buffer 里的真实坐标
    auto cursor = window.GetCursor();
    
    // 1. 计算屏幕上的物理行：真实行 - 视口顶部行 + 1 (终端的坐标是从 1 开始的，所以要 +1)
    std::size_t cursor_row = cursor.row_ - viewport.top_ + 1;
    
    // 2. 计算屏幕上的物理列：
    // 先取出光标所在的那一行文本
    std::string_view cursor_line = buffer.GetLineAt(cursor.row_);
    // 用 TextLayout.hpp 里的工具，把字符下标换算成展开 Tab 后的绝对显示列
    std::size_t absolute_display_col = BufferColumnToRenderColumn(cursor_line, cursor.column_);
    
    // 屏幕列 = 绝对显示列 - 视口左侧偏移量 + 1
    std::size_t cursor_column = absolute_display_col - viewport.left_ + 1;

    //计算cursor_row和cursor_column即可    

    frame += CursorSequence(cursor_row, cursor_column);
    frame += "\x1b[?25h";
    return frame;
}

std::string Renderer::ExpandForDisplay(std::string_view line) {
    //从左到右扫描buffer中一整行的实际内容,并扩展到render应该输出的视图
    //你应该在Render中调用这个函数,并把函数返回的结果按照视口剪切用于Render的某些行
    std::string expanded;
    expanded.reserve(line.size() * 8);
    for(char c: line){
        if(c=='\t'){
            std::size_t current_col = expanded.size();
            std::size_t spaces_to_next_tab_stop = 8 - (current_col % 8);
            expanded.append(spaces_to_next_tab_stop, ' ');

        }
        else{
            expanded.push_back(c);
        }

    }
    return expanded;
}
} // namespace sjtu
