/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/
#ifdef _WIN32
#include <direct.h>
#endif
#include <iostream>
#include <iterator>
#include <vector>
#include <map>
#include <stdexcept>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <cassert>
#include <sys/stat.h>
#include <srt.h>
#include <udt.h>
#include <common.h>

#include "apputil.hpp"
#include "uriparser.hpp"
#include "logsupport.hpp"
#include "socketoptions.hpp"
#include "transmitmedia.hpp"
#include "verbose.hpp"


#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif


using namespace std;

//　用户中断传输线程
static bool interrupt = false;
void OnINT_ForceExit(int)
{
    Verb() << "\n-------- REQUESTED INTERRUPT!\n";
    interrupt = true;
}

struct FileTransmitConfig
{   
    // 一次读取数据的最大大小，默认为 1456 字节
    unsigned long chunk_size;
    // 跳过输出文件刷新,什么意思
    bool skip_flushing;
    // 静默模式（默认关闭）
    bool quiet = false;
    // 日志级别
    srt_logging::LogLevel::type loglevel = srt_logging::LogLevel::error;
    // 日志功能区,针对SRT协议不同部分或功能模块的日志
    set<srt_logging::LogFA> logfas;
    // 输出日志文件
    string logfile;
    // 带宽报告频率，以每多少个数据包报告一次
    int bw_report = 0;
    // 状态报告频率，以每多少个数据包报告一次
    int stats_report = 0;
    // 将统计信息输出到文件
    string stats_out;
    // 统计信息的打印格式: CSV JSON ...
    SrtStatsPrintFormat stats_pf = SRTSTATS_PROFMAT_2COLS;
    // 在状态报告中包含完整的计数器（打印总统计信息）
    bool full_stats = false;

    // URI source
    string source;
    // URI target
    string target;
};


void PrintOptionHelp(const set<string> &opt_names, const string &value, const string &desc)
{
    cerr << "\t";
    int i = 0;
    for (auto opt : opt_names)
    {
        if (i++) cerr << ", ";
        cerr << "-" << opt;
    }

    if (!value.empty())
        cerr << ":" << value;
    cerr << "\t- " << desc << "\n";
}

static void printVector(const std::vector<std::string>& vec)
{
    std::cout << "[ ";
    for(size_t i = 0; i < vec.size(); i++){
        std::cout << vec[i];
        if(i < vec.size() - 1){
            std::cout << ", ";
        }
    }

    std::cout << " ]";
}


int parse_args(FileTransmitConfig &cfg, int argc, char** argv)
{
    const OptionName
        o_chunk     = { "c", "chunk" },
        o_no_flush  = { "sf", "skipflush" },
        o_bwreport  = { "r", "bwreport", "report", "bandwidth-report", "bitrate-report" },
        o_statsrep  = { "s", "stats", "stats-report-frequency" },
        o_statsout  = { "statsout" },
        o_statspf   = { "pf", "statspf" },
        o_statsfull = { "f", "fullstats" },
        o_loglevel  = { "ll", "loglevel" },
        o_logfa     = { "logfa" },
        o_logfile   = { "logfile" },
        o_quiet     = { "q", "quiet" },
        o_verbose   = { "v", "verbose" },
        o_help      = { "h", "help" },
        o_version   = { "version" };

    const vector<OptionScheme> optargs = {
        { o_chunk,        OptionScheme::ARG_ONE },
        { o_no_flush,     OptionScheme::ARG_NONE },
        { o_bwreport,     OptionScheme::ARG_ONE },
        { o_statsrep,     OptionScheme::ARG_ONE },
        { o_statsout,     OptionScheme::ARG_ONE },
        { o_statspf,      OptionScheme::ARG_ONE },
        { o_statsfull,    OptionScheme::ARG_NONE },
        { o_loglevel,     OptionScheme::ARG_ONE },
        { o_logfa,        OptionScheme::ARG_ONE },
        { o_logfile,      OptionScheme::ARG_ONE },
        { o_quiet,        OptionScheme::ARG_NONE },
        { o_verbose,      OptionScheme::ARG_NONE },
        { o_help,         OptionScheme::ARG_NONE },
        { o_version,      OptionScheme::ARG_NONE }
    };

    options_t params = ProcessOptions(argv, argc, optargs);

          bool print_help    = Option<OutBool>(params, false, o_help);
    const bool print_version = Option<OutBool>(params, false, o_version);

    if (params[""].size() != 2 && !print_help && !print_version)
    {
        cerr << "ERROR. Invalid syntax. Specify source and target URIs.\n";
        if (params[""].size() > 0)
        {
            cerr << "The following options are passed without a key: ";
            copy(params[""].begin(), params[""].end(), ostream_iterator<string>(cerr, ", "));
            cerr << endl;
        }
        print_help = true; // Enable help to print it further
    }


    if (print_help)
    {
        cout << "SRT sample application to transmit files.\n";
        PrintLibVersion();
        cerr << "Usage: srt-file-transmit [options] <input-uri> <output-uri>\n";
        cerr << "\n";

        PrintOptionHelp(o_chunk, "<chunk=1456>", "max size of data read in one step");
        PrintOptionHelp(o_no_flush, "", "skip output file flushing");
        PrintOptionHelp(o_bwreport, "<every_n_packets=0>", "bandwidth report frequency");
        PrintOptionHelp(o_statsrep, "<every_n_packets=0>", "frequency of status report");
        PrintOptionHelp(o_statsout, "<filename>", "output stats to file");
        PrintOptionHelp(o_statspf, "<format=default>", "stats printing format [json|csv|default]");
        PrintOptionHelp(o_statsfull, "", "full counters in stats-report (prints total statistics)");
        PrintOptionHelp(o_loglevel, "<level=error>", "log level [fatal,error,info,note,warning]");
        PrintOptionHelp(o_logfa, "<fas=general,...>", "log functional area [all,general,bstats,control,data,tsbpd,rexmit]");
        PrintOptionHelp(o_logfile, "<filename="">", "write logs to file");
        PrintOptionHelp(o_quiet, "", "quiet mode (default off)");
        PrintOptionHelp(o_verbose, "", "verbose mode (default off)");
        cerr << "\n";
        cerr << "\t-h,-help - show this help\n";
        cerr << "\t-version - print SRT library version\n";
        cerr << "\n";
        cerr << "\t<input-uri>  - URI specifying a medium to read from\n";
        cerr << "\t<output-uri> - URI specifying a medium to write to\n";
        cerr << "URI syntax: SCHEME://HOST:PORT/PATH?PARAM1=VALUE&PARAM2=VALUE...\n";
        cerr << "Supported schemes:\n";
        cerr << "\tsrt: use HOST, PORT, and PARAM for setting socket options\n";
        cerr << "\tudp: use HOST, PORT and PARAM for some UDP specific settings\n";
        cerr << "\tfile: file URI or file://con to use stdin or stdout\n";

        return 2;
    }

    if (Option<OutBool>(params, false, o_version))
    {
        PrintLibVersion();
        return 2;
    }

    cfg.chunk_size    = stoul(Option<OutString>(params, "1456", o_chunk));
    cfg.skip_flushing = Option<OutBool>(params, false, o_no_flush);
    cfg.bw_report     = stoi(Option<OutString>(params, "0", o_bwreport));
    cfg.stats_report  = stoi(Option<OutString>(params, "0", o_statsrep));
    cfg.stats_out     = Option<OutString>(params, "", o_statsout);
    const string pf   = Option<OutString>(params, "default", o_statspf);
    if (pf == "default")
    {
        cfg.stats_pf = SRTSTATS_PROFMAT_2COLS;
    }
    else if (pf == "json")
    {
        cfg.stats_pf = SRTSTATS_PROFMAT_JSON;
    }
    else if (pf == "csv")
    {
        cfg.stats_pf = SRTSTATS_PROFMAT_CSV;
    }
    else
    {
        cfg.stats_pf = SRTSTATS_PROFMAT_2COLS;
        cerr << "ERROR: Unsupported print format: " << pf << endl;
        return 1;
    }

    cfg.full_stats = Option<OutBool>(params, false, o_statsfull);
    cfg.loglevel   = SrtParseLogLevel(Option<OutString>(params, "error", o_loglevel));
    cfg.logfas     = SrtParseLogFA(Option<OutString>(params, "", o_logfa));
    cfg.logfile    = Option<OutString>(params, "", o_logfile);
    cfg.quiet      = Option<OutBool>(params, false, o_quiet);

    if (Option<OutBool>(params, false, o_verbose))
        Verbose::on = !cfg.quiet;

    cfg.source = params[""].at(0);
    cfg.target = params[""].at(1);


    // 遍历map,输出所有的键值对
    std::cout << "===================================" << std::endl;
    for(const auto& pair : params){

        std::cout << pair.first << " = ";
        printVector(pair.second);
        std::cout << std::endl;
    }
    std::cout << "===================================" << std::endl;

    return 0;
}


// 分离path中的路径名和文件名
void ExtractPath(string path, string& w_dir, string& w_fname)
{
    string directory = path;
    string filename = "";

    // 获取path状态
    struct stat state;
    stat(path.c_str(), &state);

    // 不是目录的情况
    if (!S_ISDIR(state.st_mode))
    {
        // Extract directory as a butlast part of path
        // 提取目录
        size_t pos = path.find_last_of("/");
        if ( pos == string::npos )
        {
            filename = path;
            directory = ".";
        }
        else
        {
            directory = path.substr(0, pos);
            filename = path.substr(pos+1);
        }
    }

    if (directory[0] != '/')
    {
        // Glue in the absolute prefix of the current directory
        // to make it absolute. This is needed to properly interpret
        // the fixed uri.
        static const size_t s_max_path = 4096; // don't care how proper this is
        char tmppath[s_max_path];
#ifdef _WIN32
        const char* gwd = _getcwd(tmppath, s_max_path);
#else
        const char* gwd = getcwd(tmppath, s_max_path);
#endif
        if ( !gwd )
        {
            // Don't bother with that now. We need something better for
            // that anyway.
            throw std::invalid_argument("Path too long");
        }
        const string wd = gwd;

        directory = wd + "/" + directory;
    }

    w_dir = directory;
    w_fname = filename;
}

bool DoUpload(UriParser& ut, string path, string filename,
              const FileTransmitConfig &cfg, std::ostream &out_stats)
{
    bool result = false;
    unique_ptr<Target> tar;                     // 独占指针
    SRTSOCKET s = SRT_INVALID_SOCK;             // SRT套接字
    bool connected = false;
    int pollid = -1;

    // ifstream = input file stream
    ifstream ifile(path, ios::binary);
    if ( !ifile )
    {
        cerr << "Error opening file: '" << path << "'";
        goto exit;
    }

    // 创建epoll实例
    pollid = srt_epoll_create();
    if ( pollid < 0 )
    {
        cerr << "Can't initialize epoll";
        goto exit;
    }

    // interrupt用于中断线程的执行
    while (!interrupt)
    {
        // Target初始化复制
        if (!tar.get())
        {
            // 根据target URI中的类型type，创建不同的对象，即SRT对象
            tar = Target::Create(ut.makeUri());

            // 创建失败
            if (!tar.get())
            {
                cerr << "Unsupported target type: " << ut.uri() << endl;
                goto exit;
            }

            // epoll事件类型，因为是仅发送，所以需要需要关注EPOLL_OUT和EPOLL_ERR两种类型
            int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
            if (srt_epoll_add_usock(pollid,
                    tar->GetSRTSocket(), &events))
            {
                cerr << "Failed to add SRT destination to poll, "
                    << tar->GetSRTSocket() << endl;
                goto exit;
            }
            srt::setstreamid(tar->GetSRTSocket(), filename);
        }

        s = tar->GetSRTSocket();
        assert(s != SRT_INVALID_SOCK);


        // srt epoll wait, 超时时间100ms
        SRTSOCKET efd;
        int efdlen = 1;
        if (srt_epoll_wait(pollid,
            0, 0, &efd, &efdlen,
            100, nullptr, nullptr, 0, 0) < 0)
        {
            continue;
        }

        assert(efd == s);
        assert(efdlen == 1);

        SRT_SOCKSTATUS status = srt_getsockstate(s);

        // 判断srt socket状态
        switch (status)
        {
            case SRTS_LISTENING:
            {
                if (!tar->AcceptNewClient())
                {
                    cerr << "Failed to accept SRT connection" << endl;
                    goto exit;
                }

                srt_epoll_remove_usock(pollid, s);

                s = tar->GetSRTSocket();
                int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                if (srt_epoll_add_usock(pollid, s, &events))
                {
                    cerr << "Failed to add SRT client to poll" << endl;
                    goto exit;
                }
                cerr << "Target connected (listener)" << endl;
                connected = true;
            }
            break;
            case SRTS_CONNECTED:
            {
                if (!connected)
                {
                    cerr << "Target connected (caller)" << endl;
                    connected = true;
                }
            }
            break;
            case SRTS_BROKEN:
            case SRTS_NONEXIST:
            case SRTS_CLOSED:
            {
                cerr << "Target disconnected" << endl;
                goto exit;
            }
            default:
            {
                // No-Op
            }
            break;
        }

        // 已经建立连接，开始传输文件
        if (connected)
        {
            // 读取文件到vertor中
            vector<char> buf(cfg.chunk_size);
            size_t n = ifile.read(buf.data(), cfg.chunk_size).gcount();
            size_t shift = 0;
            while (n > 0)
            {
                // 写入数据到目标，更新传输统计信息
                int st = tar->Write(buf.data() + shift, n, 0, out_stats);
                Verb() << "Upload: " << n << " --> " << st
                    << (!shift ? string() : "+" + Sprint(shift));
                if (st == SRT_ERROR)
                {
                    cerr << "Upload: SRT error: " << srt_getlasterror_str()
                        << endl;
                    goto exit;
                }

                n -= st;
                shift += st;
            }

            // 文件读到末尾，表示上传成功
            if (ifile.eof())
            {
                cerr << "File sent" << endl;
                result = true;
                break;
            }

            // 文件读取出错
            if ( !ifile.good() )
            {
                cerr << "ERROR while reading file\n";
                goto exit;
            }

        }
    }

    // 如果上传成功且配置中允许，则尝试清空发送缓冲区
    // 这一步操作难道是因为缓冲区中的数据可能并没有真正发送到对端？
    if (result && !cfg.skip_flushing)
    {
        assert(s != SRT_INVALID_SOCK);

        // send-flush-loop
        result = false;
        while (!interrupt)
        {
            size_t bytes;
            size_t blocks;
            int st = srt_getsndbuffer(s, &blocks, &bytes);
            if (st == SRT_ERROR)
            {
                cerr << "Error in srt_getsndbuffer: " << srt_getlasterror_str()
                    << endl;
                goto exit;
            }
            if (bytes == 0)
            {
                cerr << "Buffers flushed" << endl;
                result = true;
                break;
            }
            Verb() << "Sending buffer still: bytes=" << bytes << " blocks="
                << blocks;
            srt::sync::this_thread::sleep_for(srt::sync::milliseconds_from(250));
        }
    }

exit:
    if (pollid >= 0)
    {
        srt_epoll_release(pollid);
    }

    return result;
}

bool DoDownload(UriParser& us, string directory, string filename,
                const FileTransmitConfig &cfg, std::ostream &out_stats)
{
    bool result = false;
    unique_ptr<Source> src;                
    SRTSOCKET s = SRT_INVALID_SOCK;
    bool connected = false;
    int pollid = -1;
    string id;
    ofstream ofile;
    SRT_SOCKSTATUS status;
    SRTSOCKET efd;
    int efdlen = 1;

    // 创建epoll实例
    pollid = srt_epoll_create();
    if ( pollid < 0 )
    {
        cerr << "Can't initialize epoll";
        goto exit;
    }

    while (!interrupt)
    {
        // Source初始化
        if (!src.get())
        {
            // 根据URI创建数据源
            src = Source::Create(us.makeUri());
            if (!src.get())
            {
                cerr << "Unsupported source type: " << us.uri() << endl;
                goto exit;
            }

            // 因为仅是接收数据，所以只需要关注SRT_EPOLL_IN和SRT_EPOLL_ERR事件
            int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
            if (srt_epoll_add_usock(pollid,
                    src->GetSRTSocket(), &events))
            {
                cerr << "Failed to add SRT source to poll, "
                    << src->GetSRTSocket() << endl;
                goto exit;
            }
        }

        s = src->GetSRTSocket();
        assert(s != SRT_INVALID_SOCK);

        if (srt_epoll_wait(pollid,
            &efd, &efdlen, 0, 0,
            100, nullptr, nullptr, 0, 0) < 0)
        {
            continue;
        }

        assert(efd == s);
        assert(efdlen == 1);

        status = srt_getsockstate(s);
        Verb() << "Event with status " << status << "\n";

        switch (status)
        {
            case SRTS_LISTENING:
            {
                if (!src->AcceptNewClient())
                {
                    cerr << "Failed to accept SRT connection" << endl;
                    goto exit;
                }

                srt_epoll_remove_usock(pollid, s);

                s = src->GetSRTSocket();
                int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                if (srt_epoll_add_usock(pollid, s, &events))
                {
                    cerr << "Failed to add SRT client to poll" << endl;
                    goto exit;
                }
                id = srt::getstreamid(s);
                cerr << "Source connected (listener), id ["
                    << id << "]" << endl;
                connected = true;
                continue;
            }
            break;
            case SRTS_CONNECTED:
            {
                if (!connected)
                {
                    id = srt::getstreamid(s);
                    cerr << "Source connected (caller), id ["
                        << id << "]" << endl;
                    connected = true;
                }
            }
            break;

            // No need to do any special action in case of broken.
            // The app will just try to read and in worst case it will
            // get an error.
            case SRTS_BROKEN:
            cerr << "Connection closed, reading buffer remains\n";
            break;

            case SRTS_NONEXIST:
            case SRTS_CLOSED:
            {
                cerr << "Source disconnected" << endl;
                goto exit;
            }
            break;
            default:
            {
                // No-Op
            }
            break;
        }

        if (connected)
        {
            MediaPacket packet(cfg.chunk_size);

            if (!ofile.is_open())
            {
                const char * fn = id.empty() ? filename.c_str() : id.c_str();
                directory.append("/");
                directory.append(fn);
                ofile.open(directory.c_str(), ios::out | ios::trunc | ios::binary);

                if (!ofile.is_open())
                {
                    cerr << "Error opening file [" << directory << "]" << endl;
                    goto exit;
                }
                cerr << "Writing output to [" << directory << "]" << endl;
            }

            int n = src->Read(cfg.chunk_size, packet, out_stats);
            if (n == SRT_ERROR)
            {
                cerr << "Download: SRT error: " << srt_getlasterror_str() << endl;
                goto exit;
            }

            if (n == 0)
            {
                result = true;
                cerr << "Download COMPLETE.\n";
                break;
            }

            // Write to file any amount of data received
            Verb() << "Download: --> " << n;
            ofile.write(packet.payload.data(), n);
            if (!ofile.good())
            {
                cerr << "Error writing file" << endl;
                goto exit;
            }

        }
    }

exit:
    if (pollid >= 0)
    {
        srt_epoll_release(pollid);
    }

    return result;
}

bool Upload(UriParser& srt_target_uri, UriParser& fileuri,
            const FileTransmitConfig &cfg, std::ostream &out_stats)
{
    // 参数校验
    if ( fileuri.scheme() != "file" )
    {
        cerr << "Upload: source accepted only as a file\n";
        return false;
    }
    // fileuri is source-reading file
    // srt_target_uri is SRT target

    // 提取源文件的路径，并分离出目录和文件名
    string path = fileuri.path();
    string directory, filename;
    ExtractPath(path, (directory), (filename));
    Verb() << "Extract path '" << path << "': directory=" << directory << " filename=" << filename;
    // Set ID to the filename.
    // Directory will be preserved.

    // Add some extra parameters.
    // 设置目标URI的"transtype"参数为"file"，指示上传类型为文件
    srt_target_uri["transtype"] = "file";

    // 开始上传,即开始发送
    return DoUpload(srt_target_uri, path, filename, cfg, out_stats);
}

// 从指定源URI下载数据到本地文件
bool Download(UriParser& srt_source_uri, UriParser& fileuri,
              const FileTransmitConfig &cfg, std::ostream &out_stats)
{
    if (fileuri.scheme() != "file" )
    {
        cerr << "Download: target accepted only as a file\n";
        return false;
    }

    string path = fileuri.path(), directory, filename;
    // 分离path中的路径名和文件名
    ExtractPath(path, (directory), (filename));
    Verb() << "Extract path '" << path << "': directory=" << directory << " filename=" << filename;

    // Add some extra parameters.
    srt_source_uri["transtype"] = "file";

    return DoDownload(srt_source_uri, directory, filename, cfg, out_stats);
}


int main(int argc, char** argv)
{
    // 初始化文件传输配置结构
    FileTransmitConfig cfg;
    // 解析命令行参数
    const int parse_ret = parse_args(cfg, argc, argv);
    if (parse_ret != 0)
        return parse_ret == 1 ? EXIT_FAILURE : 0;

    //
    // Set global config variables
    //
    // 一次读取数据的最大大小
    if (cfg.chunk_size != SRT_LIVE_MAX_PLSIZE)
        transmit_chunk_size = cfg.chunk_size;

    // 统计信息的打印格式-简单工厂模式的应用实例
    transmit_stats_writer = SrtStatsWriterFactory(cfg.stats_pf);
    // 带宽报告频率，每多少个数据包报告一次
    transmit_bw_report = cfg.bw_report;
    // 状态报告频率，每多少个数据包报告一次
    transmit_stats_report = cfg.stats_report;
    // 在状态报告中包含完整的计数器（打印总统计信息）
    transmit_total_stats = cfg.full_stats;

    //
    // Set SRT log levels and functional areas
    //
    // 设置日志等级
    srt_setloglevel(cfg.loglevel);
    // 针对SRT协议不同部分或功能模块的日志
    for (set<srt_logging::LogFA>::iterator i = cfg.logfas.begin(); i != cfg.logfas.end(); ++i)
        srt_addlogfa(*i);

    //
    // SRT log handler
    //
    std::ofstream logfile_stream; // leave unused if not set
    if (!cfg.logfile.empty())
    {
        // 打开日志文件
        logfile_stream.open(cfg.logfile.c_str());
        if (!logfile_stream)
        {
            cerr << "ERROR: Can't open '" << cfg.logfile.c_str() << "' for writing - fallback to cerr\n";
        }
        else
        {
            // 设置日志流
            srt::setlogstream(logfile_stream);
        }
    }

    //
    // SRT stats output
    //
    std::ofstream logfile_stats; // leave unused if not set
    if (cfg.stats_out != "" && cfg.stats_out != "stdout")
    {
        logfile_stats.open(cfg.stats_out.c_str());
        if (!logfile_stats)
        {
            cerr << "ERROR: Can't open '" << cfg.stats_out << "' for writing stats. Fallback to stdout.\n";
            return 1;
        }
    }
    else if (cfg.bw_report != 0 || cfg.stats_report != 0)
    {
        g_stats_are_printed_to_stdout = true;
    }

    // 状态信息输出到日志还是标准输出
    ostream &out_stats = logfile_stats.is_open() ? logfile_stats : cout;

    // File transmission code

    // URI source and target
    UriParser us(cfg.source), ut(cfg.target);

    // 命令行指定了-v参数时，才会输出下面这行日志
    Verb() << "SOURCE type=" << us.scheme() << ", TARGET type=" << ut.scheme();

    // 进程终止的信号
    signal(SIGINT, OnINT_ForceExit);
    signal(SIGTERM, OnINT_ForceExit);

    try
    {
        // 源是srt,目标是file，执行下载
        if (us.scheme() == "srt")
        {
            if (ut.scheme() != "file")
            {
                cerr << "SRT to FILE should be specified\n";
                return 1;
            }
            // 下载，即接收线程
            Download(us, ut, cfg, out_stats);
        }
        // 目标是srt,源是file，执行上传
        else if (ut.scheme() == "srt")
        {
            if (us.scheme() != "file")
            {
                cerr << "FILE to SRT should be specified\n";
                return 1;
            }
            // 上传，即发送线程
            Upload(ut, us, cfg, out_stats);
        }
        else
        {
            cerr << "SRT URI must be one of given media.\n";
            return 1;
        }
    }
    catch (std::exception& x)
    {
        cerr << "ERROR: " << x.what() << endl;
        return 1;
    }


    return 0;
}
