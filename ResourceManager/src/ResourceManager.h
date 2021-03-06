#ifndef _RM_RESOURCE_MANAGER_
#define _RM_RESOURCE_MANAGER_

#include <map> // Map
#include <vector> // Vector
#include <functional> // Hash
#include <string> // String
#include <mutex>
#include <functional>
#include <thread>
#include <condition_variable>
#include <queue>


namespace MiniRM {
	class Resource;
	class FormatLoader;

	class ResourceManager {
	private:
		std::map<size_t, Resource*> m_resources;

		std::vector<FormatLoader*> m_formatLoaders;

		unsigned int m_capacityCPU;
		unsigned int m_memUsageCPU;

		unsigned int m_capacityGPU;
		unsigned int m_memUsageGPU;

		std::mutex m_mutex;

		std::hash<std::string> m_pathHasher;

		bool m_initialized;

		struct AsyncJobCallback {
			bool run;
			std::function<void(Resource*)> callback;
		};
		struct AsyncJob {
			const char* filepath;
			std::vector<AsyncJobCallback> callbacks;
		};

		std::queue<long> m_asyncJobQueue;
		std::map<long, AsyncJob> m_asyncResJobs;
		std::thread m_asyncLoadThread;
		std::condition_variable m_cond;
		std::mutex m_asyncMutex;
		std::mutex m_asyncLoadMutex;
		std::mutex m_clearJobsMutex;
		bool m_running;

	private:
		void asyncLoadStart();

	public:
		struct AsyncJobIndex {
			size_t GUID;
			unsigned int IndexOfCallback;
		};

	public:

		// ResourceManager Singleton functionalities
		static ResourceManager& getInstance() {
			static ResourceManager instance;
			return instance;
		}
		ResourceManager(ResourceManager const&) = delete;
		void operator=(ResourceManager const&) = delete;

		ResourceManager();
		~ResourceManager();
		void cleanup();
		void clearResourceManager();

		void init(const unsigned int capacityCPU, const unsigned int capacityGPU);

		Resource* load(const char* path);
		AsyncJobIndex asyncLoad(const char* path, std::function<void(Resource*)> callback);

		void removeAsyncJob(AsyncJobIndex index);
		void removeAllAsyncJobs();

		void decrementReference(size_t key);

		void registerFormatLoader(FormatLoader* formatLoader);

		unsigned int getMemUsageCPU();
		unsigned int getCapacityCPU();
		unsigned int getMemUsageGPU();
		unsigned int getCapacityGPU();

		const std::map<size_t, Resource*>& getResources() const;
		//void incrementReference(long key); SHOULDN'T BE NEEDED (done when loading already existing)
	};
}

#endif //_RM_RESOURCE_MANAGER_