// Simple example of the struct container_api usage for accessing
// information about an existing container.

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <inttypes.h>

#include "clmctfy.h"
#include "lmctfy.pb-c.h"

int main() {
	struct container_api *lmctfy = NULL;
	struct container *container = NULL;
	struct status s = {0, NULL};
	int err = 0;
	char *container_name = NULL;
	Containers__Lmctfy__ContainerStats *stats = NULL;

	err = lmctfy_new_container_api(&lmctfy, &s);
	if (err != 0) {
		printf("Failed to instantiate container_api: %s\n", s.message); 
		free(s.message);
		lmctfy_delete_container_api(lmctfy);
		return -1;
	}

	// Get what container the current thread is in.
	err = lmctfy_container_api_detect_container(lmctfy, 0, &container_name, &s);
	if (err != 0) {
		printf("Failed to detect the current container: %s\n", s.message);
		free(s.message);
		lmctfy_delete_container_api(lmctfy);
		return -1;
	}
	printf("Current container: %s\n", container_name);

	err = lmctfy_container_api_get_container(lmctfy, ".", &container, &s);
	if (err != 0) {
		printf("Failed to get container: %s\n", s.message);
		free(s.message);
		free(container_name);
		lmctfy_delete_container_api(lmctfy);
		return -1;
	}

	err = lmctfy_container_stats(container, CONTAINER_STATS_TYPE_SUMMARY, &stats, &s);
	if (err != 0) {
		printf("Failed to get container stats: %s\n", s.message);
		free(s.message);
		free(container_name);
		lmctfy_delete_container(container);
		lmctfy_delete_container_api(lmctfy);
		return -1;
	}

	printf("Memory usage: %ld\nWorking set: %ld\n",
			stats->memory->usage,
			stats->memory->working_set);
	free(container_name);
	free(stats);
	lmctfy_delete_container(container);
	lmctfy_delete_container_api(lmctfy);

	return 0;
}
