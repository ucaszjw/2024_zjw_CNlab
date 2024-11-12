#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

node_t *root;
node_advance_t *notmatch;
node_advance_t *triemap[MAP_NUM];
// return an array of ip represented by an unsigned integer, the length of array is TEST_SIZE
uint32_t* read_test_data(const char* lookup_file)
{
    // fprintf(stderr,"TODO:%s",__func__);
    FILE *fp = fopen(lookup_file, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open file %s\n", lookup_file);
        exit(1);
    }

    char line[100];
    uint32_t *ip_vec = (uint32_t *)malloc(sizeof(uint32_t) * TEST_SIZE);
    while (fgets(line, 100, fp) != NULL)
    {
        char ip_str[15];
        sscanf(line, "%s", ip_str);
        int ip_dec[4];
        sscanf(ip_str, "%d.%d.%d.%d", &ip_dec[0], &ip_dec[1], &ip_dec[2], &ip_dec[3]);
        
        *ip_vec = 0;
        for (int i = 0; i < 4; i++) 
            *ip_vec |= (ip_dec[i] << (3 - i) * 8);
        ip_vec++;
    }
    ip_vec -= TEST_SIZE;
    fclose(fp);    
    return ip_vec;
}

// Constructing an basic trie-tree to lookup according to `forward_file`
void create_tree(const char* forward_file)
{
    // fprintf(stderr,"TODO:%s",__func__);
    root = (node_t *)malloc(sizeof(node_t));
    root->type = I_NODE;
    root->lchild = NULL;
    root->rchild = NULL;

    FILE *fp = fopen(forward_file, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open file %s\n", forward_file);
        exit(1);
    }

    char line[100];
    while (fgets(line, 100, fp) != NULL) {
        char ip_str[15];
        uint32_t port, prefix;
        sscanf(line, "%s %u %u", ip_str, &prefix, &port);
        int ip_dec[4];
        sscanf(ip_str, "%d.%d.%d.%d", &ip_dec[0], &ip_dec[1], &ip_dec[2], &ip_dec[3]);
        
        uint32_t ip_bin[32];
        for (int i = 0; i < 4; i++) 
            for (int j = 7; j >= 0; j--) 
                ip_bin[i * 8 + 7 - j] = (ip_dec[i] >> j) & 1;

        node_t *cur = root;
        for (int i = 0; i < prefix; i++) {
            if (ip_bin[i] == LEFT) {
                if (cur->lchild == NULL) {
                    cur->lchild = (node_t *)malloc(sizeof(node_t));
                    cur->lchild->type = I_NODE;
                    cur->lchild->lchild = NULL;
                    cur->lchild->rchild = NULL;
                }
                cur = cur->lchild;
            }
            else if (ip_bin[i] == RIGHT) {
                if (cur->rchild == NULL) {
                    cur->rchild = (node_t *)malloc(sizeof(node_t));
                    cur->rchild->type = I_NODE;
                    cur->rchild->lchild = NULL;
                    cur->rchild->rchild = NULL;
                }
                cur = cur->rchild;
            }
            else {
                fprintf(stderr, "Error: invalid ip\n");
                exit(1);
            }
        }

        cur->type = M_NODE;
        cur->port = port;
    }

    fclose(fp);
    return;
}

// Look up the ports of ip in file `ip_to_lookup.txt` using the basic tree, input is read from `read_test_data` func 
uint32_t *lookup_tree(uint32_t* ip_vec)
{
    // fprintf(stderr,"TODO:%s",__func__);
    uint32_t *port_vec = (uint32_t *)malloc(sizeof(uint32_t) * TEST_SIZE);
    for (int i = 0; i < TEST_SIZE; i++)
    {
        uint32_t ip_bin[32];
        for (int j = 0; j < 32; j++) 
            ip_bin[j] = (ip_vec[i] >> j) & 1;
                
        node_t *cur = root;
        port_vec[i] = -1;
        int idx = 31;
        while (idx >= 0) {
            if (cur->type == M_NODE) 
                port_vec[i] = cur->port;
            
            if (ip_bin[idx] == LEFT) {
                if (cur->lchild == NULL) 
                    break;
                cur = cur->lchild;
            }
            else if (ip_bin[idx] == RIGHT) {
                if (cur->rchild == NULL) 
                    break;
                cur = cur->rchild;
            }
            else {
                fprintf(stderr, "Error: invalid ip\n");
                exit(1);
            }
            idx--;
        }
    }

    return port_vec;
}

// Constructing an advanced trie-tree to lookup according to `forward_file`
void create_tree_advance(const char* forward_file)
{
    // fprintf(stderr,"TODO:%s",__func__);
    notmatch = (node_advance_t *)malloc(sizeof(node_advance_t));
    notmatch->port = -1;
    for (int i = 0; i < MAP_NUM; i++){
        triemap[i] = (node_advance_t *)malloc(sizeof(node_advance_t));
        triemap[i]->port = 0;
        triemap[i]->prefix_diff = 0;
        triemap[i]->type = I_NODE;
        for (int j = 0; j < 4; j++)
            triemap[i]->child[j] = NULL;
    }

    FILE *fp = fopen(forward_file, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open file %s\n", forward_file);
        exit(1);
    }

    char line[100];
    while (fgets(line, 100, fp) != NULL) {
        char ip_str[15];
        uint32_t port, prefix;
        sscanf(line, "%s %u %u", ip_str, &prefix, &port);
        int ip_dec[4];
        sscanf(ip_str, "%d.%d.%d.%d", &ip_dec[0], &ip_dec[1], &ip_dec[2], &ip_dec[3]);
        
        uint32_t ip_bin = 0;
        for (int i = 0; i < 4; i++) 
            ip_bin |= (ip_dec[i] << (3 - i) * 8);
        
        if (prefix >= MAP_SHIFT){
            node_advance_t *root = triemap[(ip_bin & 0xffff0000) >> MAP_SHIFT];
            node_advance_t *cur = root, *next = NULL;
            int offset, cur_bit, cur_prefix;

            for (cur_prefix = 32-MAP_SHIFT; cur_prefix < prefix-1; cur_prefix+=2){
                offset = 30 - cur_prefix;
                cur_bit = (ip_bin >> offset) & 0x3;
                next = cur->child[cur_bit];
                if (next == NULL){
                    next = (node_advance_t *)malloc(sizeof(node_advance_t));
                    next->port = 0;
                    next->prefix_diff = 0;
                    next->type = I_NODE;
                    for (int j = 0; j < 4; j++)
                        next->child[j] = NULL;
                    cur->child[cur_bit] = next;
                }
                cur = next;
            }

            if (cur_prefix == prefix - 1){
                offset = 30 - cur_prefix;
                cur_bit = (ip_bin >> offset) & 0x3;
                int start_bit = cur_bit & 0x2;
                for (int i = 0; i < 2; i++){
                    next = cur->child[start_bit + i];
                    if (next == NULL){
                        next = (node_advance_t *)malloc(sizeof(node_advance_t));
                        next->port = port;
                        next->prefix_diff = 1;
                        next->type = M_NODE;
                        for (int j = 0; j < 4; j++)
                            next->child[j] = NULL;
                        cur->child[start_bit + i] = next;
                    }
                }
            }
            else{
                cur->port = port;
                cur->type = M_NODE;
            }
        }
        else{
            uint32_t mask = 0xffffffff << (32 - prefix);
            uint32_t start = (ip_bin & mask) >> MAP_SHIFT;
            uint32_t end = start + (1 << (MAP_SHIFT - prefix));
            for (int i = start; i < end; i++){
                if (triemap[i]->type == I_NODE || triemap[i]->prefix_diff >= 16 - prefix){
                    triemap[i]->port = port;
                    triemap[i]->type = M_NODE;
                    triemap[i]->prefix_diff = 16 - prefix;
                }
            }
        }
    }   
        
    fclose(fp);
    return;
}

// Look up the ports of ip in file `ip_to_lookup.txt` using the advanced tree input is read from `read_test_data` func 
uint32_t *lookup_tree_advance(uint32_t* ip_vec)
{
    // fprintf(stderr,"TODO:%s",__func__);
    uint32_t *port_vec = (uint32_t *)malloc(sizeof(uint32_t) * TEST_SIZE);

    for (int i = 0; i < TEST_SIZE; i++)
    {
        uint32_t ip = ip_vec[i];
        node_advance_t *cur = triemap[ip >> MAP_SHIFT];
        node_advance_t *match = notmatch;

        uint32_t cur_bit;
        int cur_prefix = 32 - MAP_SHIFT;

        if (cur->prefix_diff > 0){
            if (cur->type == M_NODE)
                match = cur;
            cur_bit = (ip >> 14) & 0x3;
            cur = cur->child[cur_bit];
            cur_prefix += 2;
        }

        while (cur != NULL){
            if (cur->type == M_NODE)
                match = cur;
            int offset = 30 - cur_prefix;
            cur_bit = (ip >> offset) & 0x3;
            cur = cur->child[cur_bit];
            cur_prefix += 2;
        }

        port_vec[i] = match->port;
    }   
        
    return port_vec;
}





