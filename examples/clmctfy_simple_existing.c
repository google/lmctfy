// Simple example of the struct container_api usage for accessing
// information about an existing container.

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <inttypes.h>

#include "clmctfy.h"
#include "lmctfy.pb-c.h"

#define CONTAINER_NAME_MAX_LENGTH	2014

int main() {
	struct container_api *lmctfy = NULL;
	struct container *container = NULL;
	struct status s = {0, NULL};
	int err = 0;
	char container_name[CONTAINER_NAME_MAX_LENGTH];
	Containers__Lmctfy__ContainerStats *stats = NULL;

	memset(container_name, 0, CONTAINER_NAME_MAX_LENGTH);

	err = lmctfy_new_container_api(&s, &lmctfy);
	if (err != 0) {
		printf("Failed to instantiate container_api: %s\n", s.message); 
		free(s.message);
		return -1;
	}

	err = lmctfy_container_api_detect_self(&s, container_name, CONTAINER_NAME_MAX_LENGTH, lmctfy);
	if (err != 0) {
		printf("Failed to detect the current container: %s\n", s.message);
		free(s.message);
		return -1;
	}
	printf("Current container: %s\n", container_name);

	err = lmctfy_container_api_get_container(&s, &container, lmctfy, ".");
	if (err != 0) {
		printf("Failed to get container: %s\n", s.message);
		free(s.message);
		return -1;
	}

	err = lmctfy_container_stats(&s, container, CONTAINER_STATS_TYPE_SUMMARY, &stats);
	if (err != 0) {
		printf("Failed to get container stats: %s\n", s.message);
		free(s.message);
		return -1;
	}

	printf("Memory usage: %ld\nWorking set: %ld\n",
			stats->memory->usage,
			stats->memory->working_set);

	return 0;
}
