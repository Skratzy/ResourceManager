#include <experimental/filesystem>
#include <new>

#include "ResourceManager.h"
#include "Defines.h"
#include "Resources/Resource.h"
#include "FormatLoaders/FormatLoader.h"
#include <fstream>

using namespace MiniRM;

// Only a single thread will ever run this function
void ResourceManager::asyncLoadStart() {
	while (m_running) {
		std::unique_lock<std::mutex> lock(m_asyncMutex);
		if (m_asyncResJobs.size() == 0 && m_running)
			m_cond.wait(lock);
		{
			// Critical region (If jobs are getting cleared)
			std::lock_guard<std::mutex> jobsClearingLock(m_clearJobsMutex);

			if (m_asyncResJobs.size() > 0 && m_running) {
				// Get the GUID for the next resource job
				long GUID = m_asyncJobQueue.front();
				m_asyncJobQueue.pop();
				// Find the job
				auto currJob = m_asyncResJobs.find(GUID);
				std::string filepath = currJob->second.filepath;
				RM_DEBUG_MESSAGE("Started Async Loading of '" + filepath + "'", 0);
				Resource* res = load(filepath.c_str());
				{
					std::lock_guard<std::mutex> critLock(m_asyncLoadMutex);
					auto size = currJob->second.callbacks.size();
					for (unsigned int i = 0; i < size; i++) {
						// If the job should still be performed, run the job
						if (currJob->second.callbacks[i].run) {
							// Increase the reference count of the resource
							res->refer();
							currJob->second.callbacks[i].callback(res);
						}
					}

					// Decreases the reference from the resource (the initial load)
					decrementReference(res->getGUID());
					//res->derefer();
					// Remove the finished job from the map
					m_asyncResJobs.erase(currJob);
					RM_DEBUG_MESSAGE("Done with async job '" + filepath + "'", 0);
				}
			}

		}
	}
}

ResourceManager::ResourceManager() {
	m_capacityCPU = 0;
	m_capacityGPU = 0;
	m_memUsageCPU = 0;
	m_memUsageGPU = 0;
	m_initialized = false;
	m_running = true;

	// Start the async loading thread
	m_asyncLoadThread = std::thread(std::bind(&ResourceManager::asyncLoadStart, this));
}


ResourceManager::~ResourceManager() {}

void ResourceManager::cleanup() {
	m_running = false;
	m_cond.notify_all();
	m_asyncLoadThread.join();
	for (auto FL : m_formatLoaders) {
		if (FL) {
			delete FL;
			FL = nullptr;
		}
	}
	m_formatLoaders.clear();
	m_formatLoaders.resize(0);
	for (auto RES : m_resources) {
		RES.second->~Resource();
		//RM_FREE(RES.second);
	}
}

void ResourceManager::clearResourceManager() {
	std::lock_guard<std::mutex> lock(m_clearJobsMutex);

	while (m_asyncJobQueue.size() > 0)
		m_asyncJobQueue.pop();

	m_asyncResJobs.clear();

	for (auto res : m_resources) {
		res.second->~Resource();
	}
	m_resources.clear();

	m_memUsageCPU = 0;
	m_memUsageGPU = 0;
}


void ResourceManager::init(const unsigned int capacityCPU, const unsigned int capacityGPU) {
	if (!m_initialized) {
		m_capacityCPU = capacityCPU;
		m_capacityGPU = capacityGPU;
		m_initialized = true;
	}
}

MiniRM::Resource* ResourceManager::load(const char* path) {

	Resource* res = nullptr;
	namespace fs = std::experimental::filesystem;
	size_t hashedPath = m_pathHasher(path);

	// Check if the resource already exists in the system
	auto it = m_resources.find(hashedPath);
	if (it != m_resources.end()) {
		// Found the resource
		res = it->second;
		res->refer();
	}
	// Else load it
	else {
		// Only one thread can create and load new resources to the resource manager

		std::lock_guard<std::mutex> lock(m_mutex);

		// Additional check if several threads tries to load the same asset it does
		// not create the same resource more than once.
		auto it = m_resources.find(hashedPath);
		if (it != m_resources.end()) {
			// Found the resource
			res = it->second;
			res->refer();
		}
		else {
			std::string ext = fs::path(path).extension().generic_string();

			// Find the format loader corresponding to the extension
			for (auto FL : m_formatLoaders) {
				// Check if the format loader supports the extension
				if (FL->extensionSupported(ext)) {

					// Load the resource and return it
					res = FL->load(path, hashedPath);

					res->setPath(path);

					// Increase the reference count of the resource
					res->refer();
					// Add the loaded resource to the map
					m_resources.emplace(hashedPath, res);

					// Update memory usage
					m_memUsageCPU += res->getSizeCPU();

					// DRAM Usage
					if (m_memUsageCPU > m_capacityCPU) {
#ifdef _DEBUG
						RM_DEBUG_MESSAGE(("ResourceManager::load() - Memory usage exceeds the memory limit on CPU. (" + std::to_string(m_memUsageCPU / (1024)) + "KB / " + std::to_string(m_capacityCPU / (1024)) + "KB) (Usage / Capacity)"), 0);
#else
						RM_DEBUG_MESSAGE(("ResourceManager::load() - Memory usage exceeds the memory limit on CPU. (" + std::to_string(m_memUsageCPU / (1024)) + "KB / " + std::to_string(m_capacityCPU / (1024)) + "KB) (Usage / Capacity)"), 0);
						RM_DEBUG_MESSAGE("Resource in memory:", 0);
						for (auto res : m_resources)
							RM_DEBUG_MESSAGE("Resource GUID: (" + std::to_string(res.second->getGUID()) + ")  Path: (" + res.second->getPath() + ")  Size: (" + std::to_string(res.second->getSizeCPU()) + " byte)", 0);
#endif
					}

					// VRAM Usage
					m_memUsageGPU += res->getSizeGPU();
					if (m_memUsageCPU > m_capacityCPU) {
#ifdef _DEBUG
						RM_DEBUG_MESSAGE(("ResourceManager::load() - Memory usage exceeds the memory limit on GPU. (" + std::to_string(m_memUsageGPU / (1024)) + "KB / " + std::to_string(m_capacityGPU / (1024)) + "KB) (Usage / Capacity)"), 0);
#else
						RM_DEBUG_MESSAGE(("ResourceManager::load() - Memory usage exceeds the memory limit on GPU. (" + std::to_string(m_memUsageGPU / (1024)) + "KB / " + std::to_string(m_capacityGPU / (1024)) + "KB) (Usage / Capacity)"), 0);
						RM_DEBUG_MESSAGE("Resource in memory:", 0);
						for (auto res : m_resources)
							RM_DEBUG_MESSAGE("Resource GUID: (" + std::to_string(res.second->getGUID()) + ")  Path: (" + res.second->getPath() + ")  Size: (" + std::to_string(res.second->getSizeGPU()) + " byte)", 0);
#endif
					}

				}
			}
		}
	}

	return res;
}

ResourceManager::AsyncJobIndex ResourceManager::asyncLoad(const char* path, std::function<void(Resource*)> callback) {
	size_t hashedPath = m_pathHasher(path);
	Resource* res = nullptr;

	// Check if the resource already exists in the system (avoid getting locked in a mutex)
	auto it = m_resources.find(hashedPath);
	if (it != m_resources.end()) {
		// Found the resource
		res = it->second;
		res->refer();
		callback(res);
		return AsyncJobIndex{ 0, 0 };
	}
	else {
		// Critical region
		std::lock_guard<std::mutex> lock(m_asyncLoadMutex);

		// Check if the resource already exists in the system (Avoid assigning the job if it was done
		//	during the lock)
		it = m_resources.find(hashedPath);
		if (it != m_resources.end()) {
			// Found the resource
			res = it->second;
			res->refer();
			callback(res);
			return AsyncJobIndex{ 0, 0 };
		}

		// Find out if the job is already queued
		auto asyncJobsIt = m_asyncResJobs.find(hashedPath);
		// The job already exists, push back another callback
		if (asyncJobsIt != m_asyncResJobs.end()) {
			asyncJobsIt->second.callbacks.push_back(AsyncJobCallback{ true, callback });
			return AsyncJobIndex{ hashedPath, unsigned int(asyncJobsIt->second.callbacks.size() - 1) };
		}
		else {
			std::vector<AsyncJobCallback> callbacks;
			callbacks.push_back(AsyncJobCallback{ true, callback });
			m_asyncResJobs.emplace(hashedPath, AsyncJob{ path, callbacks });
			m_asyncJobQueue.push(hashedPath);
			m_cond.notify_one();
			return AsyncJobIndex{ hashedPath, 0 };
		}
	}
}

void ResourceManager::removeAsyncJob(AsyncJobIndex index) {
	std::lock_guard<std::mutex> critLock(m_asyncLoadMutex);
	auto job = m_asyncResJobs.find(index.GUID);
	// if the job was found, make sure the job isn't ran
	if (job != m_asyncResJobs.end())
		job->second.callbacks.at(index.IndexOfCallback).run = false;
}

void ResourceManager::removeAllAsyncJobs() {
	std::lock_guard<std::mutex> critLock(m_asyncLoadMutex);
	for (auto& job : m_asyncResJobs)
		for (auto& callback : job.second.callbacks)
			callback.run = false;
}

void ResourceManager::decrementReference(size_t key) {
	auto resource = m_resources.find(key);
	if (resource != m_resources.end()) {
		if (resource->second->derefer() == 0) {
			RM_DEBUG_MESSAGE("Removing resource '" + std::string(resource->second->getPath()) + "'", 0);
			resource->second->~Resource();
			// Remove the resource from the map
			m_resources.erase(resource);
		}
	}
}

void ResourceManager::registerFormatLoader(FormatLoader* formatLoader) {
	// Put the new format loader in the vector
	m_formatLoaders.emplace_back(formatLoader);
}

unsigned int ResourceManager::getMemUsageCPU() {
	return m_memUsageCPU;
}

unsigned int ResourceManager::getCapacityCPU() {
	return m_capacityCPU;
}

unsigned int ResourceManager::getMemUsageGPU() {
	return m_memUsageGPU;
}

unsigned int ResourceManager::getCapacityGPU() {
	return m_capacityGPU;
}

const std::map<size_t, MiniRM::Resource*>& ResourceManager::getResources() const {
	return m_resources;
}
