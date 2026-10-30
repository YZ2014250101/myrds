#pragma once
#include <string>
typedef struct Node {
  std::string key;
  std::string value;
  Node **next;
  int level;
  Node(int level) : key(""), value(""), next(new Node *[level]), level(level) {
    for (int i = 0; i < level; i++) {
      next[i] = nullptr;
    }
  }
  Node(std::string k, std::string v, int level)
      : key(std::move(k)), value(std::move(v)), next(new Node *[level]),
        level(level) {
    for (int i = 0; i < level; ++i)
      next[i] = nullptr;
  }
  ~Node() {
    if (next) {
      delete[] next;
    }
  }
} Node;
class Skiplist {
public:
  Skiplist();
  ~Skiplist();

  // 跳表初始化
  // 输入参数：
  //    list：kv_h要传地址
  //    max_level：跳表的最大层级
  // 返回值：0成功，-1失败
  int listInit(int max_level);
  // 销毁跳表：
  // 返回值：0成功，-1失败
  int listDesy();
  // 插入元素：有冲突则是头插法
  // 返回值：0成功，-1失败，-2已经有相应的key
  int insert(std::string key, std::string value);
  // 查找元素
  // 返回值：非空成功返回节点，NULL没找到
  Node *find(const std::string &key) const;
  // 删除元素
  // 返回值：0成功，-1失败，-2没有
  int erase(const std::string &key);
  // 打印整个跳表
  // 返回值：0成功，-1失败
  int print() const;
  // 返回节点个数
  int getNodeCnt() const { return nodeCnt; }

public:
private:
  // 创建节点并初始化
  // 返回值：非空成功，NULL失败
  Node *nodeCreate(std::string key, std::string value, int level);
  // 销毁节点
  // 返回值：0成功，-1失败
  int nodeDesy(Node *node);
  int randomLevel();
  Node *head;
  int curLevel;
  int maxLevel;
  int nodeCnt;
};