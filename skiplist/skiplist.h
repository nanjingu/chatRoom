#include <iostream>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <mutex>
#include <fstream>

#define STORE_FILE "storefile"

std::mutex mtx;
std::string delimiter = ":";

template<typename K, typename V>
class Node
{
public:
    Node(){}
    Node(K k, V v, int);
    ~Node();

    K get_key() const;
    V get_value() const;

    void set_value(V);
    Node<K, V> **forward;
    int node_level;

private:
    K key;
    V value;
};

template<typename K, typename V>
Node<K, V>::Node(const K k, const V v, int level)
{
    this->key = k;
    this->value = v;
    this->node_level = level;

    this->forward = new Node<K, V>*[level + 1];
    memset(this->forward, 0, sizeof(Node<K, V>*)*(level + 1));
}

template<typename K, typename V>
Node<K, V>::~Node()
{
    delete[] forward;
}

template<typename K, typename V>
K Node<K, V>::get_key() const{
    return key;
}

template<typename K, typename V>
V Node<K, V>::get_value() const
{
    return value;
}

template<typename K, typename V>
void Node<K, V>::set_value(V value)
{
    this->value = value;
}

template<typename K, typename V>
class SkipList
{
public:
    SkipList(int);
    ~SkipList();
    int get_random_level();
    Node<K, V>* create_node(K, V, int);
    int insert_element(K, V);
    void display();
    bool search_element(K);
    void delete_element(K);
    void upload_data();
    void download_data();
    int size();

private:
    void get_data_from_string(const std::string& str, std::string* key, std::string* value);
    bool is_valid_string(const std::string& str);

    int max_level;
    int skiplist_level;
    Node<K, V>* _header;
    std::ifstream file_read;
    std::ofstream file_write;
    int element_count;
};

template<typename K, typename V>
SkipList<K, V>::SkipList(int max_level)
{
    this->max_level = max_level;
    this->skiplist_level = 0;
    this->element_count = 0;
    K k;
    V v;
    this->_header = new Node<K, V>(k, v, max_level);
}

template<typename K, typename V>
SkipList<K, V>::~SkipList()
{
    if(file_read.is_open())
        file_read.close();
    if(file_write.is_open())
        file_write.close();
    delete _header;
}


template<typename K, typename V>
Node<K, V>* SkipList<K, V>::create_node(const K k, const V v, int level)
{
    Node<K, V>* n = new Node<K, V>(k, v, level);
    return n;
}

template<typename K, typename V>
int SkipList<K, V>::get_random_level()
{
    int k = 1;
    while(rand() % 2)
        k++;
    k = (k < max_level) ? k : max_level;
    return k;
}

template<typename K, typename V>
int SkipList<K, V>::insert_element(const K key, const V value)
{
    mtx.lock();
    Node<K, V>* current = this->_header;

    Node<K, V>* update[max_level + 1];
    memset(update, 0, sizeof(Node<K, V>*)*(max_level + 1));

    for(int i = skiplist_level; i >= 0; --i)
    {
        while(current->forward[i] != NULL && current->forward[i]->get_key() < key)
            current = current->forward[i];
        update[i] = current;
    }
    current = current->forward[0];

    if(current != NULL && current->get_key() == key)
    {
        std::cout << "key: " << key <<", exist" <<std::endl;
        mtx.unlock();
        return 1;
    }
    else
    {
        int random_level = get_random_level();
        if(random_level > skiplist_level)
        {
            for(int i = skiplist_level + 1; i <= random_level; ++i)
                update[i] = _header;
            skiplist_level = random_level;
        }
        Node<K, V>* insert_node = create_node(key, value, random_level);
        for(int i = 0; i < random_level; ++i)
        {
            insert_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = insert_node;
        }
        std::cout << "insert success. key: " << key << ", value: " << value << std::endl;
        element_count++;
    }
    mtx.unlock();
    return 0;
}

template<typename K, typename V>
void SkipList<K, V>::display()
{
    std::cout << "\n******Skip List******\n" << std::endl;
    for(int i = 0; i <= skiplist_level; ++i)
    {
        Node<K, V>* node = this->_header->forward[i];
        std::cout << "Level " << i << ": ";
        while(node != NULL)
        {
            std::cout << node->get_key() << " : " << node->get_value() << " ;";
            node = node->forward[i];
        }
        std::cout << std::endl;
    }
}

template<typename K, typename V>
void SkipList<K, V>::upload_data()
{
    std::cout << "dump file------" << std::endl;
    file_write.open(STORE_FILE);
    Node<K, V>* node = this->_header->forward[0];
    while(node != NULL)
    {
        file_write << node->get_key() << " : " << node->get_value() << "\n";
        //std::cout << node->get_key() << " : " << node->get_value() << ";\n";
        node = node->forward[0];
    }
    std::cout << "upload somehing \n" << std::endl;
    file_write.flush();
    file_write.close();
}

template<typename K, typename V>
void SkipList<K, V>::download_data()
{
    file_read.open(STORE_FILE);
    std::cout << "download data--------" << std::endl;
    std::string line;
    std::string* key = new std::string();
    std::string* value = new std::string();
    while(getline(file_read, line))
    {
        get_data_from_string(line, key, value);
        if(key->empty() || value->empty())
            continue;
        insert_element(*key, *value);
        std::cout << "key: " << *key << " value: " << *value << std::endl;
    }
    file_read.close();
}

template<typename K, typename V>
int SkipList<K, V>::size()
{
    return element_count;
}

template<typename K, typename V>
void SkipList<K, V>::get_data_from_string(const std::string& str, std::string* key, std::string* value)
{
    if(!is_valid_string(str))
        return;
    *key = str.substr(0, str.find(delimiter));
    *value = str.substr(str.find(delimiter)+1, str.length());
}

template<typename K, typename V>
bool SkipList<K, V>::is_valid_string(const std::string& str)
{
    if(str.empty())
        return false;
    if(str.find(delimiter) == std::string::npos)
        return false;
    return true;
}

template<typename K, typename V>
void SkipList<K, V>::delete_element(K key)
{
    mtx.lock();
    Node<K, V>* current = this->_header;
    Node<K, V>* update[max_level + 1];
    memset(update, 0, sizeof(Node<K, V>*)*(max_level + 1));

    for(int i = skiplist_level; i >= 0; --i)
    {
        while(current->forward[i] != NULL && current->forward[i]->get_key() < key)
            current = current->forward[i];
        update[i] = current;
    }
    current = current->forward[0];
    if(current != NULL && current->get_key() == key)
    {
        for(int i = 0; i <=skiplist_level; ++i)
        {
            if(update[i]->forward[i] != current)
                break;
            update[i]->forward[i] = current->forward[i];
        }
        while(skiplist_level > 0 && _header->forward[skiplist_level] == 0)
            skiplist_level--;
        std::cout << "delete key " << key << std::endl;
        element_count--;
    }
    mtx.unlock();
    return;
}

template<typename K, typename V>
bool SkipList<K, V>::search_element(K key)
{
    std::cout << "search element--------" << std::endl;
    Node<K, V>* current = _header;
    for(int i = max_level; i >= 0; --i)
    {
        while(current->forward[i] && current->forward[i]->get_key() < key)
            current = current->forward[i];
    }
    current = current->forward[0];
    if(current && current->get_key() == key)
    {
        std::cout << "Found key: " << key << " , value: " << current->get_value() << std::endl;
        return true;
    }
    else
    {
        std::cout << "Not Found key: " << key << std::endl;
        return false;
    }
}