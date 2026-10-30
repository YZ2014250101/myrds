#include "skiplist.h"
#include <iostream>
#include <vector>
Skiplist::Skiplist()
    : head(nullptr)
    , curLevel(0)
    , maxLevel(0)
    , nodeCnt(0) { }
Skiplist::~Skiplist() {
    if (head != nullptr) {
        Node* cur = head;
        while (cur != nullptr) {
            Node* next = cur->next[0];
            delete cur;
            cur = next;
        }
    }
}
int Skiplist::listInit(int max_level) {
    if (max_level <= 0)
        return -1;
    curLevel = 1;
    maxLevel = max_level;
    nodeCnt = 0;
    head = new Node(maxLevel);
    srand(time(nullptr));
    return 0;
}
int Skiplist::insert(std::string key, std::string value) {
    std::vector<Node*> update(maxLevel, nullptr);
    Node* pre = head;
    for (int i = curLevel - 1; i >= 0; i--) {
        while (pre->next[i] != nullptr && pre->next[i]->key < key) {
            pre = pre->next[i];
        }
        update[i] = pre;
    }
    Node* cur = pre->next[0];
    if (cur != nullptr && cur->key == key) {
        cur->value = std::move(value);
        return 0;
    }

    int new_level = randomLevel();
    if (new_level > curLevel) {
        for (int i = curLevel; i < new_level; ++i) {
            update[i] = head;
        }
        curLevel = new_level;
    }

    Node* new_node = new Node(std::move(key), std::move(value), new_level);
    for (int i = 0; i < new_level; ++i) {
        new_node->next[i] = update[i]->next[i];
        update[i]->next[i] = new_node;
    }
    nodeCnt++;
    return 0;
}

int Skiplist::randomLevel() {
    int level = 1;
    while (((rand() & 0xFFFF) < 0x4000) && level < maxLevel) {
        level++;
    }
    return level;
}

Node* Skiplist::find(const std::string& key) const {
    Node* cur = head;
    for (int i = curLevel - 1; i >= 0; i--) {
        while (cur->next[i] != nullptr && cur->next[i]->key < key) {
            cur = cur->next[i];
        }
    }
    cur = cur->next[0];
    if (cur != nullptr && cur->key == key) {
        return cur;
    }
    return nullptr;
}

int Skiplist::erase(const std::string& key) {
    std::vector<Node*> update(curLevel, nullptr);
    Node* pre = head;

    // 第一步：找每一层前驱
    for (int i = curLevel - 1; i >= 0; i--) {
        while (pre->next[i] != nullptr && pre->next[i]->key < key) {
            pre = pre->next[i];
        }
        update[i] = pre;
    }

    Node* del_node = pre->next[0];
    if (del_node == nullptr || del_node->key != key) {
        return -1; // 没找到
    }

    // 只修改这个节点拥有的那些层级
    for (int i = 0; i < del_node->level; ++i) {
        update[i]->next[i] = del_node->next[i];
    }
    delete del_node;
    nodeCnt--;

    // 删除后，如果最高层索引空了，降低curLevel
    while (curLevel > 1 && head->next[curLevel - 1] == nullptr) {
        curLevel--;
    }
    return 0;
}

int Skiplist::print() const {
    for (int i = curLevel - 1; i >= 0; i--) {
        std::cout << "level " << (i + 1) << ": ";
        Node* p = head->next[i];
        while (p != nullptr) {
            std::cout << p->key << " ";
            p = p->next[i];
        }
        std::cout << std::endl;
    }
    return 0;
}
