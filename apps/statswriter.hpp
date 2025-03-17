/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


#ifndef INC_SRT_APPS_STATSWRITER_H
#define INC_SRT_APPS_STATSWRITER_H

#include <string>
#include <map>
#include <vector>
#include <memory>

#include "srt.h"
#include "utilities.h"

enum SrtStatsPrintFormat
{
    SRTSTATS_PROFMAT_INVALID = -1,      // 非法格式
    SRTSTATS_PROFMAT_2COLS = 0,         // 双列输出格式
    SRTSTATS_PROFMAT_JSON,              // json格式
    SRTSTATS_PROFMAT_CSV                // csv格式
};

SrtStatsPrintFormat ParsePrintFormat(std::string pf, std::string& w_extras);

// 状态信息类别
enum SrtStatCat
{
    SSC_GEN, //< 一般状态信息,General
    SSC_WINDOW, // 流/拥塞窗口信息, flow/congestion window
    SSC_LINK, //< 关联数据,Link data
    SSC_SEND, //< 发送状态,Sending
    SSC_RECV //< 接收状态,Receiving
};

// 状态信息数据
struct SrtStatData
{
    SrtStatCat category;    // 类别
    std::string name;       // 名称
    std::string longname;   // 长名称

    SrtStatData(SrtStatCat cat, std::string n, std::string l): category(cat), name(n), longname(l) {}
    virtual ~SrtStatData() {}

    virtual void PrintValue(std::ostream& str, const CBytePerfMon& mon) = 0;
};

// 模板类 - 定义一个指向CBytePerfMon中成员变量的指针，输出此晨光变量的值
template <class TYPE>
struct SrtStatDataType: public SrtStatData
{
    // 指向CBytePerfMon中成员变量的指针
    typedef TYPE CBytePerfMon::*pfield_t;
    pfield_t pfield;

    SrtStatDataType(SrtStatCat cat, const std::string& name, const std::string& longname, pfield_t field)
        : SrtStatData (cat, name, longname), pfield(field)
    {
    }

    void PrintValue(std::ostream& str, const CBytePerfMon& mon) override
    {
        str << mon.*pfield;
    }
};

// 抽象基类-SRT状态统计
class SrtStatsWriter
{
public:
    virtual std::string WriteStats(int sid, const CBytePerfMon& mon) = 0;
    virtual std::string WriteBandwidth(double mbpsBandwidth) = 0;
    virtual ~SrtStatsWriter() {}

    // Only if HAS_PUT_TIME. Specified in the imp file.
    std::string print_timestamp();

    // 添加key=value
    void Option(const std::string& key, const std::string& val)
    {
        options[key] = val;
    }

    // 获取key对应value的指针
    bool Option(const std::string& key, std::string* rval = nullptr)
    {
        const std::string* out = map_getp(options, key);
        if (!out)
            return false;

        if (rval)
            *rval = *out;
        return true;
    }

protected:
    std::map<std::string, std::string> options;
};

extern std::vector<std::unique_ptr<SrtStatData>> g_SrtStatsTable;

std::shared_ptr<SrtStatsWriter> SrtStatsWriterFactory(SrtStatsPrintFormat printformat);



#endif
