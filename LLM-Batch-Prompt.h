#pragma once

class BatchPrompter
{
public:
	BatchPrompter() {};
	bool LoadConfig();
	bool LoadNextModel();
	bool PingModel();
	bool HasProces(bool firstLevel);
	bool GetOutput(bool write = true);
	bool WriteOutput();
	bool KillModel(DWORD currentProcessId = GetCurrentProcessId());
	std::string getModelName() { return models[currentModel]; }
	void IncModel() { currentModel++; }
private:
	std::vector<PROCESSENTRY32> GetProcesses(DWORD currentProcessId);
	double GetDedicatedGPUMemory(DWORD processId);
	double GetVram();
	double GetRam();
	void LoadConfigLines();
	void LoadPrompts();

	std::string Generate(std::string prompt, int max_length=100);

	std::vector<std::string> GetValues(const std::string& name) const;
	std::string GetDefaultString(std::string configName, std::string defaultValue) { if (configLines.find(configName) == configLines.end()) return defaultValue; return configLines[configName]; };
	int GetDefaultInt(std::string configName, int defaultValue) { if (GetDefaultString(configName, "").empty()) return defaultValue; return stoi(GetDefaultString(configName, "")); }
	bool GetDefaultBool(std::string configName, bool defaultValue) { return GetDefaultInt(configName, defaultValue); };

	std::map<std::string, std::string> configLines;
	int currentModel = 0;
	std::string koboldPath, modelPath, startupParams, promptFile, promptSeperator, promptUrl, promptParams, outputFile;
	bool showKoboldOutput;
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> response;
	std::unordered_map < std::string, std::string> promptParam;
	std::vector<std::string> models;
	std::vector<std::string> params;
	std::vector<std::string> prompts;
};

void WriteStringsToFile(const std::string& filePath, const std::vector<std::string>& strings);
