#include "file_manager.h"

FileManager::FileManager() 
{
	initBinPath();
	mEngineRoot = mBinPath + ENGINE_RELATIVE_ROOT;
	mResourceDir = mEngineRoot + RESOURCE_DIR;
	mShaderDir = mEngineRoot + SHADER_DIR;
	mModelsDir = mEngineRoot + MODELS_DIR;
	mCacheDir = mEngineRoot + CACHE_DIR;
	LOG("SHADER DIR:" << mShaderDir);
}

FileManager& FileManager::getInstance()
{
	static FileManager fileManager;
	return fileManager;
}

std::string FileManager::getFilePath(const char* filename) 
{
	std::string s(filename);
	return FileManager::getFilePath(s);
}

std::string FileManager::getFilePath(const std::string& filename)
{
	return filename.substr(0, filename.find_last_of("\\/"));
}

std::string FileManager::getResourcePath(std::string&& path) 
{
	return FileManager::getInstance().mResourceDir + path;
}

std::string FileManager::getModelsPath(std::string&& path) 
{
	return FileManager::getInstance().mModelsDir + path;
}

std::string FileManager::getCachePath(std::string&& path) 
{
	return FileManager::getInstance().mCacheDir + path;
}



std::vector<char> FileManager::readShader(const std::string& shaderName)
{
	std::string filename = FileManager::getInstance().mShaderDir + shaderName + ".spv";
	LOG("SHADER FILE NAME:" << filename);
	return readFile(filename);
}


std::vector<char> FileManager::readFile(const std::string& filename) 
{
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) 
		throw std::runtime_error("failed to open file!");

	size_t fileSize = (size_t) file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

std::vector<char> FileManager::readCache(const std::string& cacheName)
{
	std::string filename = FileManager::getInstance().mCacheDir + cacheName;
	LOG("CACHE FILE NAME:" << filename);
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		LOG("CACHE CANNOT BE OPENED");
		std::vector<char> buffer;
		return buffer;
	} 


	size_t fileSize = (size_t) file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();
	LOG("RETURNING CACHED: " << fileSize);
	return buffer;
}


void FileManager::writeCache(const char* cacheName, void* data, size_t size)
{

	std::string filename = FileManager::getInstance().mCacheDir + cacheName;
	LOG("CACHE FILE NAME:" << filename);

	std::ofstream binFile(filename, std::ios::out | std::ios::binary);
    if (!binFile.is_open())
    	throw std::runtime_error("failed to open cache!");

	binFile.write((char*) &data, size);
    binFile.close();
	LOG("CACHE WRITTEN");
}


void FileManager::initBinPath()
{
	#ifdef WINDOWS
		char buff[MAX_PATH];
	#else
		char buff[PATH_MAX];
	#endif
    size_t sz = sizeof(buff)-1;
    LOG("IN BIN ABS PATH");
    #ifdef WINDOWS
        if (!GetModuleFileName(NULL, buff, sz)) 
			throw std::runtime_error("Could not read read binary file directory");
    #else 
        ssize_t len;
        if ((len = readlink("/proc/self/exe", buff, sz)) == -1) 
			throw std::runtime_error("Could not read read binary file directory");    
        buff[len] = '\0';
    #endif
    LOG("B ABS:" << std::string(buff));
    mBinPath = std::string(buff);
	mBinPath = getFilePath(mBinPath);
}

std::string FileManager::getBinPath() 
{
	return mBinPath;
}
