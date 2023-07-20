// LLM-Batch-Prompt.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "HTTPRequest.hpp"
#include <regex>
#include <json/json.h>
#include <TlHelp32.h>
#include <fstream>
#include "LLM-Batch-Prompt.h"

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
        http::Request request{ promptUrl };

        Json::Value body;

        body["prompt"] = prompt;
        for (auto param : promptParam)
        {
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
            param += modelStr[i];

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

    std::string params = "--model "+ modelPath + models[currentModel] + " " + startupParams;

    // Set the process command-line parameters
    const char* processPath = koboldPath.c_str();
    const char* processArgs = params.c_str();

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

bool BatchPrompter::HasModel(DWORD currentProcessId)
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
                return true;
            }
        } while (Process32Next(hSnapshot, &processEntry));
    }

    // Close the process snapshot handle
    CloseHandle(hSnapshot);

    return false;
}

bool BatchPrompter::GetOutput(bool write)
{
    if (!showKoboldOutput)
        std::cout << "Processing prompt:";
    int p = 0, pm = prompts.size();
    for (auto prompt : prompts)
    {
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

    if (!showKoboldOutput)
        std::cout << "\rPrompts done.                                                   " << std::endl;

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
            
            replyVal["model"] = models.first;
            replyVal["reply"].append(models.second);
            for (auto l : StrSplit(models.second, "\n"))
            {
                std::string line = l;
                trim(line);
                if (!line.empty())
                    replyVal["replyLines"].append(line);
            }
            
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
    if (!showKoboldOutput && currentProcessId == GetCurrentProcessId())
        std::cout << "Unloading model: " << models[currentModel] << " " << params[currentModel] << std::endl;

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
            if (!bp.HasModel())
                return 1;

            if (retry > 100)
                return 1;
        }

        if (!bp.GetOutput())
            return 1;
       
        if (!bp.KillModel())
            return 1;

        bp.IncModel();
    }
    
    return 0;
}