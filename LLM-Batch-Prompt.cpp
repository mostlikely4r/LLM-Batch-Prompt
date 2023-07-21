// LLM-Batch-Prompt.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include <iostream>
#include "HTTPRequest.hpp"
#include <regex>
#include <json/json.h>
#include <TlHelp32.h>
#include <fstream>
#include "LLM-Batch-Prompt.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <iomanip>
#include <locale>
#include <codecvt>
#include <chrono>


static inline void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
        }));
}

// trim from end (in place)
static inline void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
        }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string& s) {
    rtrim(s);
    ltrim(s);
}

std::vector<std::string> StrSplit(const std::string& src, const std::string& sep)
{
    std::vector<std::string> r;
    std::string s;
    for (char i : src)
    {
        if (sep.find(i) != std::string::npos)
        {
            if (s.length()) r.push_back(s);
            s.clear();
        }
        else
        {
            s += i;
        }
    }
    if (s.length()) r.push_back(s);
    return r;
}

std::string replaceNewLines(const std::string& input) {
    std::string output = input;
    std::regex newlineRegex("\\\\n");
    output = std::regex_replace(output, newlineRegex, "\n");
    return output;
}

std::string BatchPrompter::Generate(std::string prompt, int max_length)
{
    try
    {
        int contextSize = 2048;
        if (startupParams.find("--contextsize ") != std::string::npos)
            contextSize = stoi(startupParams.substr(startupParams.find("--contextsize ") + std::string("--contextsize ").size(), 4));
        if (params[currentModel].find("--contextsize ") != std::string::npos)
            contextSize = stoi(params[currentModel].substr(params[currentModel].find("--contextsize ") + std::string("--contextsize ").size(), 4));

        http::Request request{ promptUrl };

        Json::Value body;

        body["prompt"] = prompt;
        for (auto param : promptParam)
        {
            if (param.first == "max_context_length" && stoi(param.second) > contextSize)
            {
                body[param.first] = contextSize;
                continue;
            }

            if (param.second.find("'") == 0)
            {
                body[param.first] = param.second.substr(1,param.second.size()-2);
            }
            else if(param.second.find(".") != std::string::npos)
                body[param.first] = stof(param.second);
            else
                body[param.first] = stoi(param.second);

        }

        //std::string bodyString = body.toStyledString();
        Json::StreamWriterBuilder writer;
        writer.settings_["precision"] = 6;
        std::string bodyString = Json::writeString(writer, body);

        const auto response = request.send("POST", bodyString, {
            {"Content-Type", "application/json"}
            }, std::chrono::minutes{ 10 });

        std::string jsonString{ response.body.begin(), response.body.end() };

        Json::Value root;
        Json::Reader reader;
        bool parsingSuccessful = reader.parse(jsonString, root);
        if (!parsingSuccessful) {
            return "Error: Failed to parse the JSON string:" + jsonString;
        }

        std::string textValue = root["results"][0]["text"].asString();
        std::string replacedText = replaceNewLines(textValue);

        return replacedText;
    }
    catch (const std::exception& e)
    {
        return  "Error: " + std::string(e.what()) + '\n';
    }
}

std::vector<std::string> BatchPrompter::GetValues(const std::string& name) const
{
    std::vector<std::string> values;
    for (auto entry : configLines)
        if (entry.first.find(name) == 0)
            values.push_back(entry.first);

    return values;
};

bool BatchPrompter::LoadConfig()
{
    LoadConfigLines();

    koboldPath = GetDefaultString("koboldPath", "C:\\Games\\AI\\kobolcpp\\koboldcpp.exe");
    modelPath = GetDefaultString("modelPath", "C:\\Games\\AI\\oobabooga_windows\\text-generation-webui\\models\\");
    startupParams = GetDefaultString("defaultParams", "--usecublas 0 0 --threads 10 --highpriority --smartcontext --usemirostat 2 0.1 0");
    promptFile = GetDefaultString("promptFile", "prompts.txt");
    promptSeperator = GetDefaultString("promptSeperator", "<prompt_end>");
    promptUrl = GetDefaultString("promptUrl", "http://127.0.0.1:5001/api/v1/generate");
    promptParams = GetDefaultString("promptParams", "");
    showKoboldOutput = GetDefaultBool("showKoboldOutput", true);
    outputFile = GetDefaultString("outputFile", "output.txt");

    for (auto modelnr : GetValues("modelName"))
    {
        std::vector<std::string> modelStr = StrSplit(configLines[modelnr], " ");
        models.push_back(modelStr[0]);
        std::string param = "";
        for (unsigned int i = 1; i < modelStr.size(); i++)
            param += " " + modelStr[i];

        param = param.substr(1);

        params.push_back(param);
    }

    for (auto param : StrSplit(promptParams, ","))
    {
        promptParam[StrSplit(param, ":")[0]] = StrSplit(param, ":")[1];
    }

    LoadPrompts();

    return true;
}

void BatchPrompter::LoadConfigLines()
{
    std::vector<std::string> retVec;

    char result[MAX_PATH];
    GetModuleFileNameA(NULL, result, (sizeof(result)));
    std::string ppath(result);

    ppath = ppath.substr(0, ppath.find_last_of("\\/"));

    std::ifstream in(ppath + "/config.conf", std::ifstream::in);
    if (in.fail())
        return;

    do
    {
        std::string line;
        std::getline(in, line);

        if (!line.length())
            continue;

        if (line.find("#") == 0)
            continue;

        std::vector<std::string> op = StrSplit(line, "=");

        if (op.size() != 2)
            continue;

        trim(op[0]);
        trim(op[1]);

        configLines[op[0]] = op[1];
    } while (in.good());
    in.close();
}

void BatchPrompter::LoadPrompts()
{
    std::vector<std::string> retVec;

    char result[MAX_PATH];
    GetModuleFileNameA(NULL, result, (sizeof(result)));
    std::string ppath(result);

    ppath = ppath.substr(0, ppath.find_last_of("\\/"));

    std::ifstream in(ppath + "/" + promptFile, std::ifstream::in);
    if (in.fail())
        return;

    std::string prompt;

    do
    {
        std::string line;
        std::getline(in, line);

        if (line.find("<prompt_end>") == 0)
        {
            prompts.push_back(prompt);
            prompt.clear();
            continue;
        }

        if (!prompt.empty())
            prompt += "\n";

        prompt += line;
    } while (in.good());
    in.close();

    if(!prompt.empty())
        prompts.push_back(prompt);
}

bool BatchPrompter::LoadNextModel()
{
    if (currentModel > (int)models.size() - 1)
        return false;

    std::string strParams = "--model "+ modelPath + models[currentModel] + " " + params[currentModel] + " " + startupParams;

    // Set the process command-line parameters
    const char* processPath = koboldPath.c_str();
    const char* processArgs = strParams.c_str();

    // Create the process
    STARTUPINFOA startupInfo;
    PROCESS_INFORMATION processInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    ZeroMemory(&processInfo, sizeof(processInfo));
    startupInfo.cb = sizeof(startupInfo);
    if (!showKoboldOutput)
    {
        startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        startupInfo.dwFlags |= STARTF_USESTDHANDLES;
        std::cout << "Loading model: " << models[currentModel] << std::endl;
    }
        

    if (!CreateProcessA(processPath, const_cast<LPSTR>(processArgs), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo)) {
        std::cout << "Failed to create the process. Error code: " << GetLastError() << std::endl;
        return false;
    }
    
    return true;
}

bool BatchPrompter::PingModel()
{
    try
    {
        // you can pass http::InternetProtocol::V6 to Request to make an IPv6 request
        http::Request request{ "http://127.0.0.1:5001/api/extra/version" };

        // send a get request
        const auto response = request.send("GET", "", {}, std::chrono::seconds{ 5 });
        return true;
    }
    catch (const std::exception& e)
    {
        std::string error = "Error: " + std::string(e.what()) + '\n';
        return false;
    }
}

std::vector<PROCESSENTRY32> BatchPrompter::GetProcesses(DWORD currentProcessId)
{
    // Create a snapshot of the current running processes
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::cout << "Failed to create process snapshot. Error code: " << GetLastError() << std::endl;
        return {};
    }

    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    std::vector<PROCESSENTRY32> ret;
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

    // Iterate through the processes in the snapshot
    if (Process32First(hSnapshot, &processEntry)) {
        do {
            // Check if the parent process ID matches the current process ID
            if (processEntry.th32ParentProcessID == currentProcessId) {
                if(koboldPath.find(converter.to_bytes(processEntry.szExeFile)) != std::string::npos)
                ret.push_back(processEntry);
                
                for(auto p : GetProcesses(processEntry.th32ProcessID))
                    ret.push_back(p);
            }
        } while (Process32Next(hSnapshot, &processEntry));
    }

    // Close the process snapshot handle
    CloseHandle(hSnapshot);

    return ret;
}

inline LPCWSTR StringToLPCWSTR(const std::string& str) {
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    wchar_t* buffer = new wchar_t[size];
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, buffer, size);
    return buffer;
}

double BatchPrompter::GetDedicatedGPUMemory(DWORD processId) {
    double memVal;
    PDH_HQUERY hQuery;
    PDH_HCOUNTER hCounter;

    // Initialize PDH
    if (PdhOpenQuery(nullptr, 0, &hQuery) != ERROR_SUCCESS) {
        return 0;
    }
    std::string proc = "\\GPU Process Memory(pid_" + std::to_string(processId) + "_luid_0x00000000_0x0000e137_phys_0)\\Dedicated Usage";
    if (PdhAddCounter(hQuery, StringToLPCWSTR(proc), 0, &hCounter) != ERROR_SUCCESS) {
        PdhCloseQuery(hQuery);
        return 0;
    }

    // Collect data and display CPU usage every second
    PDH_FMT_COUNTERVALUE counterValue;
    DWORD dwType;
    if (PdhCollectQueryData(hQuery) != ERROR_SUCCESS) {
        PdhCloseQuery(hQuery);
        return 0;
    }

    if (PdhGetFormattedCounterValue(hCounter, PDH_FMT_DOUBLE, &dwType, &counterValue) == ERROR_SUCCESS) {
        memVal = counterValue.doubleValue;
    }
    else {
        return 0;
    }

    // Clean up
    PdhRemoveCounter(hCounter);
    PdhCloseQuery(hQuery);

    return memVal;
}

double BatchPrompter::GetVram()
{
    double vramUsage = 0;

    for (auto p : GetProcesses(GetCurrentProcessId()))
    {
        vramUsage = GetDedicatedGPUMemory(p.th32ProcessID);
    }
    return vramUsage;
}

double BatchPrompter::GetRam()
{
    double ramUsage = 0;
    for (auto p : GetProcesses(GetCurrentProcessId()))
    {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, p.th32ProcessID);
        if (hProcess == nullptr) {
            std::cout << "Failed to open process." << std::endl;
            return 1;
        }

        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            ramUsage += pmc.WorkingSetSize;

        }

        CloseHandle(hProcess);
    }

    return ramUsage;
}

bool BatchPrompter::HasProces(bool firstLevel)
{
    std::vector<PROCESSENTRY32> procs = GetProcesses(GetCurrentProcessId());

    if (firstLevel && !procs.empty())
        return true;

    for (auto p : procs)
        if (p.th32ParentProcessID != GetCurrentProcessId())
            return true;

    return false;
}

inline std::string FloatToStringWithPrecision(double value, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

bool BatchPrompter::GetOutput(bool write)
{
    auto start_time = std::chrono::high_resolution_clock::now();

    if (!showKoboldOutput)
        std::cout << "Processing prompt:";
    int p = 0, pm = prompts.size();
    for (auto prompt : prompts)
    {
        if (!PingModel())
            break;

        if (!showKoboldOutput)
            std::cout << "\rProcessing prompt:" << std::to_string(p+1) << "/" << std::to_string(pm);

        response[prompt][models[currentModel]] = Generate(prompt);

        if (write)
        {
            if (!WriteOutput())
                return false;
        }

        p++;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    int minutes = std::chrono::duration_cast<std::chrono::minutes>(duration_ms).count();
    int seconds = (int)std::chrono::duration_cast<std::chrono::seconds>(duration_ms % std::chrono::minutes(1)).count();


    if (!showKoboldOutput)
        std::cout << "\rPrompts done. (VRAM: " + FloatToStringWithPrecision(GetVram()/1024/1000/1000,3) + " GB, RAM: "+ FloatToStringWithPrecision(GetRam() / 1024 / 1000 / 1000, 3) + " GB, Runtime: " << minutes << " min " << seconds << " sec) " << std::endl;

    return true;
}

bool BatchPrompter::WriteOutput() {
    char result[MAX_PATH];
    GetModuleFileNameA(NULL, result, (sizeof(result)));
    std::string ppath(result);

    ppath = ppath.substr(0, ppath.find_last_of("\\/"));
    ppath += "/";
    ppath += outputFile;

    std::ofstream outputFile(ppath, std::ios::out | std::ios::trunc);
    if (!outputFile.is_open()) {
        std::cout << "Failed to open the file for writing." << std::endl;
        return false;
    }

    Json::Value outputVal;

    for (auto prompt : response)
    {
        Json::Value promptVal;

        promptVal["prompt"] = prompt.first;

        for (auto models : prompt.second)
        {
            Json::Value replyVal;
            
            replyVal["reply"]= models.second;

            replyVal["used model"] = models.first;
           
            promptVal["replies"].append(replyVal);
        }

        outputVal.append(promptVal);
    }

    std::string jsonString = outputVal.toStyledString();

    outputFile << jsonString << std::endl;

    outputFile.close();  

    return true;
}

bool BatchPrompter::KillModel(DWORD currentProcessId)
{
    // Create a snapshot of the current running processes
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::cout << "Failed to create process snapshot. Error code: " << GetLastError() << std::endl;
        return {};
    }

    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    // Iterate through the processes in the snapshot
    if (Process32First(hSnapshot, &processEntry)) {
        do {
            // Check if the parent process ID matches the current process ID
            if (processEntry.th32ParentProcessID == currentProcessId) {
                KillModel(processEntry.th32ProcessID);

                if (processEntry.th32ParentProcessID != GetCurrentProcessId())
                {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processEntry.th32ProcessID);
                    if (hProcess != NULL) {
                        // Terminate the process
                        if (!TerminateProcess(hProcess, 0)) {
                            std::cout << "Failed to terminate process with ID " << processEntry.th32ProcessID << ". Error code: " << GetLastError() << std::endl;
                        }
                        CloseHandle(hProcess);
                    }
                }
            }
        } while (Process32Next(hSnapshot, &processEntry));
    }

    // Close the process snapshot handle
    CloseHandle(hSnapshot);

    return true;
}

int main()
{
    BatchPrompter bp;

    if (!bp.LoadConfig())
        return 1;

    while(bp.LoadNextModel())
    {
        int retry = 0;
        while (!bp.PingModel())
        {
            if (!bp.HasProces(true))
            {
                retry = -1;
                break;
            }

            if (retry > 100)
                return 1;

            retry++;
        }
        
        if (retry == -1)
        {
            std::cout << "Error with model " << bp.getModelName() << ", Skipping." << std::endl;

            if (!bp.KillModel())
                return 1;

            bp.IncModel();
            continue;
        }

        if (!bp.GetOutput())
            return 1;
     
        if (!bp.KillModel())
            return 1;

        bp.IncModel();
    }
    
    return 0;
}