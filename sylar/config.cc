/**
 * @file config.cc
 * @brief 配置模块实现
 * @version 0.1
 * @date 2021-06-14
 */
#include "sylar/config.h"
#include "sylar/env.h"
#include "sylar/util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

ConfigVarBase::ptr Config::LookupBase(const std::string &name) {
    RWMutexType::ReadLock lock(GetMutex());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

//"A.B", 10
//A:
//  B: 10
//  C: str
// 递归处理YAML树的每一级
static void ListAllMember(const std::string &prefix,
                          const YAML::Node &node,
                          std::list<std::pair<std::string, const YAML::Node>> &output) {
    if (prefix.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678") != std::string::npos) {
        SYLAR_LOG_ERROR(g_logger) << "Config invalid name: " << prefix << " : " << node;
        return;
    }
    output.push_back(std::make_pair(prefix, node));
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            ListAllMember(prefix.empty() ? it->first.Scalar() : prefix + "." + it->first.Scalar(),
                          it->second, output);
        }
    }
}

void Config::LoadFromYaml(const YAML::Node &root) {
    std::list<std::pair<std::string, const YAML::Node>> all_nodes;
    ListAllMember("", root, all_nodes);

    for (auto &i : all_nodes) {
        std::string key = i.first;
        if (key.empty()) {  //根节点不处理
            continue;
        }

        std::transform(key.begin(), key.end(), key.begin(), ::tolower); //转小写字母
        ConfigVarBase::ptr var = LookupBase(key);   //取的已有的key的配置项
        if (var) { //更新配置值
            if (i.second.IsScalar()) { //数字类型
                var->fromString(i.second.Scalar());
            } else {//复杂类型
                std::stringstream ss;
                ss << i.second;
                var->fromString(ss.str());
            }
        }
    }
}

/// 记录每个文件的修改时间
static std::map<std::string, uint64_t> s_file2modifytime;
/// force表示：是否强制加载配置文件，非强制加载的情况下，如果记录的文件修改时间未变化，则跳过该文件的加载
static sylar::Mutex s_mutex;
void Config::LoadFromConfDir(const std::string &path, bool force) {
    std::string absoulte_path = sylar::EnvMgr::GetInstance()->getAbsolutePath(path);
    std::vector<std::string> files;
    FSUtil::ListAllFile(files, absoulte_path, ".yml");  //获得所有.yml配置文件路径

    for (auto &i : files) {
        {
            struct stat st;
            lstat(i.c_str(), &st);  //获取文件信息（主要是获取修改时间）
            sylar::Mutex::Lock lock(s_mutex);
            if (!force && s_file2modifytime[i] == (uint64_t)st.st_mtime) {
                continue;
            }
            s_file2modifytime[i] = st.st_mtime;
        }
        try {
            YAML::Node root = YAML::LoadFile(i);
            LoadFromYaml(root);
            SYLAR_LOG_INFO(g_logger) << "LoadConfFile file="
                                     << i << " ok";
        } catch (...) {
            SYLAR_LOG_ERROR(g_logger) << "LoadConfFile file="
                                      << i << " failed";
        }
    }
}

void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) {
    RWMutexType::ReadLock lock(GetMutex());
    ConfigVarMap &m = GetDatas();
    for (auto it = m.begin(); it != m.end(); ++it) {
        cb(it->second);
    }
}

} // namespace sylar
