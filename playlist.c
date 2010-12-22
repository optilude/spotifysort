/**
 * Copyright (c) 2006-2010 Spotify Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

// #define TESTING

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <libspotify/api.h>

#include "playlist.h"

typedef struct s_playlist_item {
	int index;
	int end_index; // for folders
	const char *name;	
} playlist_item;

typedef struct s_node {
	struct s_node *next;
	
	struct s_node *children;
	struct s_node *parent;
	
	playlist_item *item;
} node;


/** Node operations **/

static node *create_node(node *previous, node *parent, playlist_item *item) {
	node *new_node = (node *)malloc(sizeof(node));
	
	new_node->item = item;
	
	new_node->next = NULL;
	
	new_node->parent = parent;
	new_node->children = NULL;
	
	if(previous != NULL) {
		previous->next = new_node;
	}
	
	if(parent != NULL) {
		if(parent->children == NULL) {
			parent->children = new_node;
		}
	}
	
	return new_node;
}

static void free_list(node *head) {
	if (head->next != NULL) {
		free_list(head->next);
	}
	if (head->children != NULL) {
		free_list(head->children);
	}
	if (head->item != NULL) {
		if(head->item->name != NULL) {
			free((void *)head->item->name);
		}
		free(head->item);
	}
	free(head);
}

static void print_list(node *head) {
	
	playlist_item *item = head->item;
	
	printf("%03d (%03d) %s\n", item->index, item->end_index, item->name);
	printf("  next:     %s\n", head->next     == NULL? "null" : head->next->item->name);
	printf("  parent:   %s\n", head->parent   == NULL? "null" : head->parent->item->name);
	printf("  children: %s\n", head->children == NULL? "null" : head->children->item->name);
	
	if(head->children != NULL) {
		print_list(head->children);
	}
	
	if(head->next != NULL) {
		print_list(head->next);
	}
	
}

/** Merge sort **/

static node *merge(node *head_one, node *head_two);

static node *merge_sort(node *head) {
	node *head_one;
	node *head_two;
	
	if((head == NULL) || (head->next == NULL)) 
		return head;
	
	head_one = head;
	head_two = head->next;
	while((head_two != NULL) && (head_two->next != NULL)) {
		head = head->next;
		head_two = head->next->next;
	}
	head_two = head->next;
	head->next = NULL;
	
	return merge(merge_sort(head_one), merge_sort(head_two));
}

static node *merge(node *head_one, node *head_two) {
	node *head_three;
	
	if(head_one == NULL) 
		return head_two;
	
	if(head_two == NULL) 
		return head_one;
	
	if(strcmp(head_one->item->name, head_two->item->name) < 0) {
		head_three = head_one;
		head_three->next = merge(head_one->next, head_two);
	} else {
		head_three = head_two;
		head_three->next = merge(head_one, head_two->next);
	}
	
	return head_three;
}

static node *sort_list(node *head) {
	node *n;
	
	for(n = head; n != NULL; n = n->next) {
		if(n->children != NULL) {
			n->children = sort_list(n->children);
		}
	}
	
	return merge_sort(head);
}

/** Flatten for reordering **/

static void _flatten_list(node *head, int *reorder, int *idx) {
	reorder[(*idx)++] = head->item->index;
	
	if(head->children != NULL) {
		_flatten_list(head->children, reorder, idx);
		reorder[(*idx)++] = head->item->end_index;
	}
	
	if(head->next != NULL) {
		_flatten_list(head->next, reorder, idx);
	}
}

static void flatten_list(node *head, int *reorder) {
	int idx = 0;
	_flatten_list(head, reorder, &idx);
#ifdef TESTING
	printf("Did %d iterations\n", idx + 1);
#endif
}

static void recalculate_indexes(int *reorder, int size, int moved) {
	int original_index, i;
	
	original_index = reorder[moved];
	for(i = moved; i < size; ++i) {
		if(reorder[i] < original_index) {
			++reorder[i];
		}
	}
	
}

/** Playlist item operations **/

static playlist_item *create_playlist_item(int index, const char *name) {
	playlist_item *new_playlist_item = (playlist_item *) malloc(sizeof(playlist_item));
	if (NULL != new_playlist_item){
		new_playlist_item->index = index;
		new_playlist_item->end_index = -1;
		new_playlist_item->name = strdup(name);
	}
	return new_playlist_item;
}


#ifdef TESTING
static void move_playlist(playlist_item *faux_playlist, int size, int from_index, int to_index) {
	int i;
	playlist_item item;
	
	item.name = faux_playlist[from_index].name;
	item.index = faux_playlist[from_index].index;
	item.end_index = faux_playlist[from_index].end_index;
	
	if(from_index < to_index) {
		// shift each item between from_index + 1 and to_index down one
		for(i = from_index + 1; i <= to_index; ++i) {
			faux_playlist[i-1].name = faux_playlist[i].name;
			faux_playlist[i-1].index = faux_playlist[i].index;
			faux_playlist[i-1].end_index = faux_playlist[i].end_index;
		}
	} else if(from_index > to_index) {
		// shift each item between from_index - 1 and to_index up one
		for(i = from_index - 1; i >= to_index; --i) {
			faux_playlist[i+1].name = faux_playlist[i].name;
			faux_playlist[i+1].index = faux_playlist[i].index;
			faux_playlist[i+1].end_index = faux_playlist[i].end_index;
		}
	}
	
	faux_playlist[to_index].name = item.name;
	faux_playlist[to_index].index = item.index;
	faux_playlist[to_index].end_index = item.end_index;
	
}
#endif

/**
 * Move playlists
 */
int sort_playlists(sp_session *session)
{
	sp_playlistcontainer *pc = sp_session_playlistcontainer(session);
	sp_playlist_type playlist_type;
	int i, not_loaded = 0, num_playlists = 0;
	int *reorder;
	sp_playlist *pl;
	node *items, *parent, *previous;
	
	
#ifdef TESTING
	playlist_item *faux_playlist;
#endif
	
	num_playlists = sp_playlistcontainer_num_playlists(pc);
	items = previous = parent = NULL;
	
#ifdef TESTING
	faux_playlist = (playlist_item*) malloc(sizeof(playlist_item) * num_playlists);
#endif

	printf("Reordering %d playlists and playlist folders\n", num_playlists);
	
	for (i = 0; i < num_playlists; ++i) {
		playlist_type = sp_playlistcontainer_playlist_type(pc, i);
		
		switch (playlist_type) {
				
			case SP_PLAYLIST_TYPE_PLAYLIST:
				
				pl = sp_playlistcontainer_playlist(pc, i);
				if (!sp_playlist_is_loaded(pl)) {
					not_loaded++;
				} else {
					previous = create_node(previous, parent, create_playlist_item(i, sp_playlist_name(pl)));
					if (items == NULL) {
						items = previous;
					}
				}
				
#ifdef TESTING
				faux_playlist[i].index = -1;
				faux_playlist[i].name = strdup(sp_playlist_name(pl));
#endif
				
				break;
			case SP_PLAYLIST_TYPE_START_FOLDER:
				
				parent = create_node(previous, parent, create_playlist_item(i, sp_playlistcontainer_playlist_folder_name(pc, i)));
				previous = NULL;
				if (items == NULL) {
					items = parent;
				}
				
#ifdef TESTING
				faux_playlist[i].index = sp_playlistcontainer_playlist_folder_id(pc, i);
				faux_playlist[i].name = strdup(sp_playlistcontainer_playlist_folder_name(pc, i));
#endif
				
				break;
			case SP_PLAYLIST_TYPE_END_FOLDER:
				
				previous = parent;
				previous->item->end_index = i;
				parent = parent->parent;
				
#ifdef TESTING
				faux_playlist[i].index = sp_playlistcontainer_playlist_folder_id(pc, i);
				faux_playlist[i].name = NULL;
#endif
				
				break;
			case SP_PLAYLIST_TYPE_PLACEHOLDER:

#ifdef TESTING
				printf("%d. Placeholder", i);
				faux_playlist[i].index = -1;
				faux_playlist[i].name = NULL;
#endif
				
				break;
		}
	}
	
	if(not_loaded > 0) {
		printf("ERROR: %d playlists could not be loaded\n", not_loaded);
		return 1;
	}
	
	if(items != NULL) {
		items = sort_list(items);

#ifdef TESTING
		print_list(items);
#endif
		
		reorder = (int *) malloc(sizeof(int) * num_playlists);
		flatten_list(items, reorder);
		
		for(i = 0; i < num_playlists; ++i) {
			printf(".");
			if(i != reorder[i]) {
#ifdef TESTING
				printf("Moving item at %d -> %d\n", reorder[i], i);
				move_playlist(faux_playlist, num_playlists, reorder[i], i);
#else			
				sp_playlistcontainer_move_playlist(pc, reorder[i], i);
#endif
				recalculate_indexes(reorder, num_playlists, i);
			}
		}
		printf("\ndone\n");
		
		free(reorder);
		free_list(items);
	}
	
	
#ifdef TESTING
	for(i = 0; i < num_playlists; ++i) {
		if(faux_playlist[i].name != NULL) {
			printf(" -- %s (%d)\n", faux_playlist[i].name, faux_playlist[i].index);
			free((void*)faux_playlist[i].name);
		} else {
			printf(" -- %d\n", faux_playlist[i].index);
		}
	}
	free(faux_playlist);
#endif
	
	return 1;
}
