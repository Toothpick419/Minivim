#include <fstream>

#include "Buffer.hpp"

namespace sjtu {

    // 1. 构造函数：根据路径打开并读取文件
Buffer::Buffer(const std::filesystem::path& path){
    path_ = path;
    if (path.empty()) {
        lines_.push_back("");
    }
    else {
        std::ifstream file(path);// 去硬盘里读这个文件
        if (file.is_open()){
            std::string line;
            while(getline(file,line)) { //每读一行存到line中 并返回true
                lines_.push_back(line);
            }
            if(lines_.empty()) {
                lines_.push_back("");
            }
        }
        else {
            lines_.push_back("");// 文件不存在时，当做空文件处理
        }
    }
    is_modified_ = false;
    //从path指向的文件构造Buffer,你需要打开文件并且把文件内容填充进Buffer,并正确初始化一些状态.
    //注意path可能为空的边界情况
}

// 2. 构造函数：直接用现成的 lines 数组和路径来创建
Buffer::Buffer(std::vector<std::string> lines, std::filesystem::path path) {
    lines_=lines;
    path_ = path;
    if (lines_.empty()) {
        lines_.push_back("");
    }
    is_modified_ = false;
}

std::size_t Buffer::GetLineCount() const {
    //返回文件行数
    return lines_.size();
}

const std::string& Buffer::GetLineAt(std::size_t row) const {
    //返回第row行的内容
    return lines_[row];
}


std::string Buffer::GetDisplayName() const {
    //返回文件名,若是新文件,返回"[No Name]"
    if (path_.empty()) {
        return "[No Name]";
    }
    else {
        return path_.filename().string();
    }
}

bool Buffer::IsModified() const {
    //返回文件和上次保存比起来是否被修改过
    return is_modified_;
}

void Buffer::InsertCharacter(std::size_t row, std::size_t column, char value) {
    //在第row行第col列插入一个value, 注意越界检查
   if (row>=lines_.size()) return;
   if(column > lines_[row].size() ){
     column = lines_[row].size();
   }
   lines_[row].insert(column,1,value);
   is_modified_=true;
}

void Buffer::EraseCharacter(std::size_t row, std::size_t column) {
   //在第row行第col列删除一个value
   if (row >= lines_.size() || column >= lines_[row].size()) return;
    
    // 核心动作：去第 row 行，从 column 的位置开始，删除 1 个字符
    lines_[row].erase(column, 1);
    is_modified_ = true;
}

void Buffer::SplitLine(std::size_t row, std::size_t column) {
    //在第row行第col列分割,即在此处敲了回车键
    if (row>=lines_.size()) return;
    if(column > lines_[row].size() ){
      column = lines_[row].size();
    }
    std::string right_part=lines_[row].substr(column);
    lines_[row].erase(column);
    lines_.insert(lines_.begin()+row+1,right_part);
    is_modified_ = true;
}

void Buffer::JoinLine(std::size_t row) {
   //把第row + 1行合并进第row行
   if (row + 1 >= lines_.size()) return;
   lines_[row] += lines_[row + 1];
   lines_.erase(lines_.begin() + row + 1);
    
    is_modified_ = true;
}

void Buffer::Save() {
   //把文件内容保存, 直接调用WriteTo方法
   WriteTo(path_);
}

void Buffer::SaveAs(const std::filesystem::path& path) {
    path_=path;
    WriteTo(path_);
}


void Buffer::WriteTo(const std::filesystem::path& path) const {
   //实际将缓冲区中的内容写入path指向的文件中
   std::ofstream file(path); // 使用 ofstream 打开文件准备写入。如果文件已存在，默认会清空原内容重新写
   if (file.is_open()) {
    // 遍lines_ 数组里的每一行
     for(const std::string &line : lines_){
        // 把这一行的文字写进去，并且手动补上一个换行符 '\n'
        file << line << '\n';
     }
     is_modified_ = false;
   }
}

} // namespace sjtu
