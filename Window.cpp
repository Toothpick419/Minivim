#include "Window.hpp"
#include "TextLayout.hpp"

#include <algorithm>
#include <cctype>

namespace sjtu {


void Window::Resize(ScreenSize terminal_size) {
    //底部留一行给命令或提示,其余作为正文区域;正文行数和列数都至少取1
    //修改视口即可
    viewport_.rows_=(terminal_size.rows_>1) ? (terminal_size.rows_-1) : 1;
    viewport_.columns_=(terminal_size.columns_>1)?(terminal_size.columns_-1):1;
}

void Window::ApplyMotion(const Buffer& buffer, Motion motion) {
    //1. 根据方向调用对应的移动函数,Basic中每次移动一步,在Advanced中你可以改变count/添加别的case
    //2. 将行列限制在Normal模式的合法范围内(Buffer应始终至少有一行)
    //3. 调整视口,让移动后的光标可见(EnsureCursorVisible)
    switch (motion) {
        case Motion::Left:
            if (cursor_.column_ > 0) cursor_.column_--;
            break;
        case Motion::Right:
            cursor_.column_++;
            break;
        case Motion::Up:
            if (cursor_.row_ > 0) cursor_.row_--;
            break;
        case Motion::Down:
            cursor_.row_++;
            break;
    }
    std::size_t max_row= (buffer.GetLineCount() >0) ? (buffer.GetLineCount()-1) : 0;
    if( cursor_.row_ > max_row){
        cursor_.row_ = max_row;
    }
    std::size_t line_length = buffer.GetLineAt(cursor_.row_).size();
    std::size_t max_col= (line_length>0) ? (line_length-1) : 0;
    if( cursor_.column_ > max_col){
        cursor_.column_ = max_col;
    }
    EnsureCursorVisible(buffer);
}

void Window::EnsureCursorVisible(const Buffer& buffer) {
    //1. 光标高于或低于可见区域时,调整top_,使光标刚好进入区域
    //2. 把光标的字符下标换算成显示列,再用相同思路调整left_
    if (cursor_.row_<viewport_.top_){
        viewport_.top_=cursor_.row_;
    }
    else if (cursor_.row_>=viewport_.top_+viewport_.rows_){
        viewport_.top_ = cursor_.row_ - viewport_.rows_ + 1;
    }
    const std::string& line = buffer.GetLineAt(cursor_.row_);
    std::size_t display_col = BufferColumnToRenderColumn(line, cursor_.column_);
    if (display_col < viewport_.left_) {
        viewport_.left_ = display_col;
    } 
    else if (display_col >= viewport_.left_ + viewport_.columns_) {
        // 让光标刚好贴在屏幕的最右侧
        viewport_.left_ = display_col - viewport_.columns_ + 1;
    }
    }


const Position& Window::GetCursor() const {
    //返回当前光标位置的只读引用
    return cursor_;
}

const Viewport& Window::GetViewport() const {
    //返回当前可见区域的只读引用,供Renderer绘制
    return viewport_;
}


void Window::SetCursor(const Buffer& buffer, Position position, bool allow_line_end) {
    //1. 先限制行号,再根据该行长度和allow_line_end限制列号
    //2. 用新位置更新上下移动时的目标显示列
    //3. 调整视口,保证光标可见
    std::size_t line_count = buffer.GetLineCount();
    if (line_count == 0) {
        cursor_.row_ = 0;
        cursor_.column_ = 0;
        desired_column_ = 0;
        EnsureCursorVisible(buffer);
        return;
    } 
    // 限制行号不越界
    std::size_t row = std::min(position.row_, line_count - 1);
    // 获取当前行的文本长度
    const std::string& line = buffer.GetLineAt(row);
    std::size_t line_len = line.size();
    // 限制列号
    std::size_t max_col = 0;
    if (allow_line_end) {
        // 允许停在最后一个字符之后（插入模式）
        max_col = line_len;
    } 
    else
    {
        // 必须停在字符上，空行为 0
        max_col = (line_len > 0) ? (line_len - 1) : 0 ;
    }
    std::size_t col = std::min(position.column_, max_col);

    cursor_ = {row, col};

    // 2. 用新位置更新上下移动时的目标显示列
    // 【修改这里】：用新位置更新上下移动时的目标显示列
    desired_column_ = BufferColumnToRenderColumn(line, col);

    // 3. 调整视口,保证光标可见
    EnsureCursorVisible(buffer);
}


void Window::MoveLeft(const Buffer& buffer, std::size_t count) {
    //向左移动count个字符,最多到行首,并更新目标显示列
    if (count >= cursor_.column_) {
        cursor_.column_ = 0; // 如果移动步数大于等于当前列号，直接卡在行首
    } 
    else
    {
        cursor_.column_ -= count; 
     // 否则正常向左移动
    }
    // 2. 更新上下移动时的目标显示列 (保持和当前所在列一致)
    const std::string& line = buffer.GetLineAt(cursor_.row_);
    desired_column_ = BufferColumnToRenderColumn(line, cursor_.column_);
    // 3. 调整视口，保证光标可见 (如果向左移动出了屏幕边界，视口会自动往左滚)
    EnsureCursorVisible(buffer);
}
void Window::MoveRight(const Buffer& buffer, std::size_t count) {
    //向右移动count个字符,最多到最后一个字符,并更新目标显示列
    std::size_t line_len = buffer.GetLineAt(cursor_.row_).size(); 
    // 如果是空行，光标只能待在第 0 列
    if (line_len == 0) {
        cursor_.column_ = 0;
    } 
    else
    {
        // 向右移动最多只能到最后一个字符，即 line_len - 1
        std::size_t max_col = line_len - 1;
        // 将当前列加上移动步数，如果超出 max_col，就停在 max_col
        cursor_.column_ = std::min(cursor_.column_ + count, max_col);
    }
    // 2. 更新上下移动时的目标显示列 (保持和当前所在列一致)
    const std::string& line = buffer.GetLineAt(cursor_.row_);
    desired_column_ = BufferColumnToRenderColumn(line, cursor_.column_);
    // 3. 调整视口，保证光标可见 (如果向右移动出了屏幕边界，视口会自动往右滚)
    EnsureCursorVisible(buffer);
}


void Window::MoveUp(const Buffer& buffer, std::size_t count) {
    //先算目标行,最多到第一行,再将期望的显示列换算成目标行的字符下标
    //经过短行时不要更新desired_screen_column_,这样继续移动到长行时能回到原来的列
    if (count >= cursor_.row_) {
        cursor_.row_ = 0; // 如果移动步数大于等于当前行号，直接卡在第一行
    } 
    else
    {
        cursor_.row_ -= count; 
    }
    std::size_t line_len = buffer.GetLineAt(cursor_.row_).size(); 
    std::size_t max_col = (line_len >0 ) ? (line_len - 1) : 0;
    const std::string& line = buffer.GetLineAt(cursor_.row_);
    std::size_t target_col = RenderColumnToBufferColumn(line, desired_column_);
    // 取目标下标和该行最大合法下标的较小值
    cursor_.column_ = std::min(target_col, max_col);
    EnsureCursorVisible(buffer);
}

void Window::MoveDown(const Buffer& buffer, std::size_t count) {
    //先算目标行,最多到最后一行,再根据desired_screen_column_寻找目标字符
    //与向上移动一样,保留期望显示列
    std::size_t line_count = buffer.GetLineCount(); 
    //  先算目标行, 最多只能到最后一行 (line_count - 1)
    std::size_t max_row = line_count - 1;
    cursor_.row_ = std::min(cursor_.row_ + count, max_row);

    std::size_t line_len = buffer.GetLineAt(cursor_.row_).size(); 
    // 计算这行允许的最大列号（处理空行的边界情况）
    std::size_t max_col = (line_len > 0) ? (line_len - 1) : 0;
    // 光标实际所在的列，是“期望列”和“这行最大列”中较小的一个
    const std::string& line = buffer.GetLineAt(cursor_.row_);
    std::size_t target_col = RenderColumnToBufferColumn(line, desired_column_);
    
    // 取目标下标和该行最大合法下标的较小值
    cursor_.column_ = std::min(target_col, max_col);
    EnsureCursorVisible(buffer);
}

} // namespace sjtu
