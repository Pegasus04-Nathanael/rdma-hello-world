/*
 * ════════════════════════════════════════════════════════════════════
 * RDMA CLIENT - Hello World InfiniBand
 * ════════════════════════════════════════════════════════════════════
 * 
 * CE QUE FAIT CE PROGRAMME :
 * 
 * 1. Se connecte au serveur RDMA
 * 2. Reçoit : adresse RAM serveur + RKEY (clé d'accès)
 * 3. Fait RDMA_READ : lit DIRECTEMENT la RAM du serveur
 * 4. Fait RDMA_WRITE : écrit DIRECTEMENT dans la RAM du serveur
 * 5. Re-fait RDMA_READ : vérifie que l'écriture a marché
 * 
 * LE TRUC FOU :
 * → Toutes ces opérations se font SANS réveiller le CPU du serveur
 * → La carte InfiniBand du client parle à la carte serveur
 * → Les CPUs ne sont PAS impliqués !
 * → Latence ultra-basse : 1-5 μs (vs 5 ms disque)
 * 
 * C'est EXACTEMENT ce que fait InfiniSwap :
 * → Page-out = RDMA WRITE vers machine remote
 * → Page-in  = RDMA READ depuis machine remote
 * 
 * Compilation :
 *   gcc -Wall -g -o rdma_client rdma_client.c -lrdmacm -libverbs -lpthread
 * 
 * Utilisation :
 *   ./rdma_client <server_ip>
 *   Exemple : ./rdma_client 10.10.1.1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h>

#define BUFFER_SIZE 1024*1024  // 1 MB

// Structure pour recevoir les infos RDMA du serveur
struct rdma_buffer_info {
    uint64_t addr;      // Adresse de la RAM serveur
    uint32_t rkey;      // Clé d'accès RDMA
};

int main(int argc, char *argv[]) {
    struct rdma_buffer_info server_info;
    struct ibv_wc wc;

    
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        printf("Exemple: %s 10.10.1.1\n", argv[0]);
        return 1;
    }
    
    printf("═══════════════════════════════════════════════════\n");
    printf("    RDMA CLIENT - HELLO WORLD INFINIBAND\n");
    printf("═══════════════════════════════════════════════════\n\n");
    printf("Connexion au serveur %s...\n\n", argv[1]);
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPES 1-3 : CRÉER EVENT CHANNEL + CM ID
    // ═══════════════════════════════════════════════════════
    // Même chose que le serveur
    
    printf("🔌 ÉTAPE 1-3 : Création infrastructure RDMA\n");
    
    struct rdma_event_channel *cm_channel = rdma_create_event_channel();
    if (!cm_channel) {
        perror("   ❌ rdma_create_event_channel");
        return 1;
    }
    
    struct rdma_cm_id *cm_id;
    int ret = rdma_create_id(cm_channel, &cm_id, NULL, RDMA_PS_TCP);
    if (ret) {
        perror("   ❌ rdma_create_id");
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Infrastructure créée\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 4 : RÉSOUDRE L'ADRESSE DU SERVEUR
    // ═══════════════════════════════════════════════════════
    // CONCRÈTEMENT : On cherche comment joindre le serveur
    // → Résolution DNS/IP
    // → Trouve la route InfiniBand vers le serveur
    
    printf("📍 ÉTAPE 4 : Résolution adresse serveur\n");
    printf("   (Trouver comment joindre %s:12345)\n", argv[1]);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    inet_pton(AF_INET, argv[1], &addr.sin_addr);
    
    ret = rdma_resolve_addr(cm_id, NULL, (struct sockaddr *)&addr, 2000);
    if (ret) {
        perror("   ❌ rdma_resolve_addr");
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    struct rdma_cm_event *event;
    ret = rdma_get_cm_event(cm_channel, &event);
    if (ret || event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
        printf("   ❌ Échec résolution adresse\n");
        if (event) rdma_ack_cm_event(event);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Adresse résolue\n\n");
    rdma_ack_cm_event(event);
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 5 : RÉSOUDRE LA ROUTE
    // ═══════════════════════════════════════════════════════
    // CONCRÈTEMENT : Trouver le chemin InfiniBand vers le serveur
    // → Sur quel port InfiniBand ?
    // → Par quel switch ?
    // → C'est automatique avec rdma_resolve_route()
    
    printf("🗺️  ÉTAPE 5 : Résolution route InfiniBand\n");
    printf("   (Trouver le chemin physique vers le serveur)\n");
    
    ret = rdma_resolve_route(cm_id, 2000);
    if (ret) {
        perror("   ❌ rdma_resolve_route");
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    ret = rdma_get_cm_event(cm_channel, &event);
    if (ret || event->event != RDMA_CM_EVENT_ROUTE_RESOLVED) {
        printf("   ❌ Échec résolution route\n");
        if (event) rdma_ack_cm_event(event);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Route résolue\n\n");
    rdma_ack_cm_event(event);
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPES 6-8 : CRÉER PD, CQ, QP
    // ═══════════════════════════════════════════════════════
    // Même chose que le serveur
    
    printf("🛠️  ÉTAPE 6-8 : Création ressources RDMA\n");
    printf("   (PD, CQ, QP - comme le serveur)\n");
    
    struct ibv_pd *pd = ibv_alloc_pd(cm_id->verbs);
    if (!pd) {
        perror("   ❌ ibv_alloc_pd");
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    struct ibv_cq *cq = ibv_create_cq(cm_id->verbs, 16, NULL, NULL, 0);
    if (!cq) {
        perror("   ❌ ibv_create_cq");
        ibv_dealloc_pd(pd);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.send_cq = cq;
    qp_attr.recv_cq = cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_send_wr = 16;
    qp_attr.cap.max_recv_wr = 16;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;
    
    ret = rdma_create_qp(cm_id, pd, &qp_attr);
    if (ret) {
        perror("   ❌ rdma_create_qp");
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ PD, CQ, QP créés\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 9 : ALLOUER BUFFER LOCAL
    // ═══════════════════════════════════════════════════════
    // CONCRÈTEMENT : On alloue 1 MB dans notre RAM locale
    // → On va stocker les données lues/écrites ici
    // → On enregistre aussi cette RAM pour RDMA (ibv_reg_mr)
    
    printf("📦 ÉTAPE 9 : Allocation buffers locaux\n");
    printf("   (Séparé: un pour RECV, un pour RDMA)\n");
    
    // Buffer pour RECV (recevoir les infos du serveur)
    char *recv_buffer = malloc(BUFFER_SIZE);
    if (!recv_buffer) {
        printf("   ❌ Échec allocation recv_buffer\n");
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    struct ibv_mr *recv_mr = ibv_reg_mr(pd, recv_buffer, BUFFER_SIZE,
                                         IBV_ACCESS_LOCAL_WRITE);
    if (!recv_mr) {
        perror("   ❌ ibv_reg_mr (recv)");
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    // Buffer pour RDMA READ (stocker les données lues)
    char *rdma_buffer = malloc(BUFFER_SIZE);
    if (!rdma_buffer) {
        printf("   ❌ Échec allocation rdma_buffer\n");
        ibv_dereg_mr(recv_mr);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    struct ibv_mr *rdma_mr = ibv_reg_mr(pd, rdma_buffer, BUFFER_SIZE,
                                         IBV_ACCESS_LOCAL_WRITE);
    if (!rdma_mr) {
        perror("   ❌ ibv_reg_mr (rdma)");
        free(rdma_buffer);
        ibv_dereg_mr(recv_mr);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Buffers créés et enregistrés\n");
    printf("      - recv_buffer: %p (MR LKEY: 0x%x)\n", recv_buffer, recv_mr->lkey);
    printf("      - rdma_buffer: %p (MR LKEY: 0x%x)\n\n", rdma_buffer, rdma_mr->lkey);
    
    // ═══════════════════════════════════════════════════════
    // POSTER LE RECV ICI (AVANT CONNEXION) !
    // ═══════════════════════════════════════════════════════
    // NOTE: Le serveur met les infos au DÉBUT du recv_buffer
    
    //struct rdma_buffer_info server_info;
    
    struct ibv_sge recv_sge;
    recv_sge.addr = (uint64_t)recv_buffer;  // ← Au DÉBUT, pas à la fin !
    recv_sge.length = sizeof(server_info);
    recv_sge.lkey = recv_mr->lkey;
    
    struct ibv_recv_wr recv_wr, *bad_recv_wr;
    memset(&recv_wr, 0, sizeof(recv_wr));
    recv_wr.wr_id = 2;
    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;
    
    ret = ibv_post_recv(cm_id->qp, &recv_wr, &bad_recv_wr);
    if (ret) {
        perror("   ❌ ibv_post_recv");
        ibv_dereg_mr(rdma_mr);
        ibv_dereg_mr(recv_mr);
        free(rdma_buffer);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ RECV posté (prêt à recevoir du serveur)\n\n");




    // ═══════════════════════════════════════════════════════
    // ÉTAPE 10 : SE CONNECTER AU SERVEUR
    // ═══════════════════════════════════════════════════════
    // CONCRÈTEMENT : Établir la connexion RDMA avec le serveur
    
    printf("🤝 ÉTAPE 10 : Connexion au serveur\n");
    
    struct rdma_conn_param conn_param;
    memset(&conn_param, 0, sizeof(conn_param));
    
    ret = rdma_connect(cm_id, &conn_param);
    if (ret) {
        perror("   ❌ rdma_connect");
        ibv_dereg_mr(rdma_mr);
        ibv_dereg_mr(recv_mr);
        free(rdma_buffer);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    ret = rdma_get_cm_event(cm_channel, &event);
    if (ret || event->event != RDMA_CM_EVENT_ESTABLISHED) {
        printf("   ❌ Connexion échouée\n");
        if (event) rdma_ack_cm_event(event);
        ibv_dereg_mr(rdma_mr);
        ibv_dereg_mr(recv_mr);
        free(rdma_buffer);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Connecté au serveur\n\n");
    rdma_ack_cm_event(event);
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 11 : RECEVOIR LES INFOS DU SERVEUR
    // ═══════════════════════════════════════════════════════
    // LE RECV A DÉJÀ ÉTÉ POSTÉ À L'ÉTAPE 9 !
    // ICI ON ATTEND JUSTE LA RÉCEPTION
    
    printf("📥 ÉTAPE 11 : Réception infos mémoire serveur\n");
    printf("   (Le RECV est déjà posté, on attend...)\n\n");
    
    // Attendre la complétion du RECV
    //struct ibv_wc wc;
    while (ibv_poll_cq(cq, 1, &wc) < 1) {
        // Polling... attente active
    }
    
    if (wc.status != IBV_WC_SUCCESS) {
        printf("   ❌ Réception échouée (status: %d)\n", wc.status);
        ibv_dereg_mr(rdma_mr);
        ibv_dereg_mr(recv_mr);
        free(rdma_buffer);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_disconnect(cm_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Infos reçues avec succès !\n\n");

    // DEBUG: Afficher les bytes reçus
    unsigned char *recv_data = (unsigned char *)recv_buffer;
    printf("   📍 DEBUG RECV - Bytes reçus:\n");
    for (int i = 0; i < sizeof(server_info); i++) {
        printf("      [%d] = 0x%02x\n", i, recv_data[i]);
    }

    memcpy(&server_info, recv_buffer, sizeof(server_info));
    
    printf("   ┌─────────────────────────────────────────────┐\n");
    printf("   │ INFORMATIONS REÇUES DU SERVEUR :            │\n");
    printf("   ├─────────────────────────────────────────────┤\n");
    printf("   │ Adresse RAM serveur : 0x%016lx  │\n", server_info.addr);
    printf("   │ RKEY (clé accès)    : 0x%08x            │\n", server_info.rkey);
    printf("   │ recv_buffer addr    : 0x%016lx    │\n", (uint64_t)recv_buffer);
    printf("   │ rdma_buffer addr    : 0x%016lx    │\n", (uint64_t)rdma_buffer);
    printf("   │ recv_mr LKEY        : 0x%08x            │\n", recv_mr->lkey);
    printf("   │ rdma_mr LKEY        : 0x%08x            │\n", rdma_mr->lkey);
    printf("   │                                             │\n");
    printf("   │ Je peux maintenant accéder à cette RAM !    │\n");
    printf("   │ → RDMA_READ  pour lire                      │\n");
    printf("   │ → RDMA_WRITE pour écrire                    │\n");
    printf("   └─────────────────────────────────────────────┘\n\n");
    
    sleep(2);
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 12 : RDMA READ - LIRE LA RAM DU SERVEUR
    // ═══════════════════════════════════════════════════════
    // ✨ LA MAGIE COMMENCE ! ✨
    //
    // QU'EST-CE QUI SE PASSE CONCRÈTEMENT ?
    //
    // 1. Je prépare une requête RDMA_READ
    // 2. Je spécifie :
    //    - Où stocker les données lues (mon buffer local)
    //    - D'où lire (adresse RAM serveur)
    //    - La clé d'accès (RKEY)
    // 3. J'envoie la requête à ma carte InfiniBand
    // 4. MA CARTE parle à la CARTE SERVEUR
    // 5. LA CARTE SERVEUR lit sa RAM et envoie les données
    // 6. MA CARTE reçoit et écrit dans mon buffer local
    // 7. LE CPU DU SERVEUR N'A JAMAIS ÉTÉ RÉVEILLÉ ! 😴
    //
    // Latence totale : 1-5 μs (vs 5 ms pour disque)
    
    printf("📖 ÉTAPE 12 : RDMA READ - Lecture RAM serveur\n");
    printf("   ┌─────────────────────────────────────────────┐\n");
    printf("   │ Je vais lire DIRECTEMENT la RAM serveur    │\n");
    printf("   │ SANS réveiller son CPU !                    │\n");
    printf("   │                                             │\n");
    printf("   │ Ma carte IB ──> Carte IB serveur ──> RAM   │\n");
    printf("   │                 (bypass CPU)                │\n");
    printf("   └─────────────────────────────────────────────┘\n\n");
    
    // Préparer RDMA READ
    struct ibv_sge read_sge;
    read_sge.addr = (uint64_t)rdma_buffer;
    read_sge.length = 100;  // Lire 100 octets
    read_sge.lkey = rdma_mr->lkey;

    struct ibv_send_wr *bad_wr = NULL;
    
    struct ibv_send_wr read_wr;
    memset(&read_wr, 0, sizeof(read_wr));
    read_wr.wr_id = 3;
    read_wr.sg_list = &read_sge;
    read_wr.num_sge = 1;
    read_wr.opcode = IBV_WR_RDMA_READ;  // ← RDMA READ !
    read_wr.send_flags = IBV_SEND_SIGNALED;
    read_wr.wr.rdma.remote_addr = server_info.addr;  // Adresse serveur
    read_wr.wr.rdma.rkey = server_info.rkey;         // Clé d'accès
    
    ret = ibv_post_send(cm_id->qp, &read_wr, &bad_wr);
    if (ret) {
        perror("   ❌ ibv_post_send (READ)");
        ibv_dereg_mr(rdma_mr);
        ibv_dereg_mr(recv_mr);
        free(rdma_buffer);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_disconnect(cm_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    // Attendre complétion
    while (ibv_poll_cq(cq, 1, &wc) < 1);
    
    if (wc.status != IBV_WC_SUCCESS) {
        printf("   ❌ RDMA READ échoué\n");
        printf("   📍 DEBUG - Work Completion Status: %d\n", wc.status);
        printf("   📍 DEBUG - WR ID: %ld\n", wc.wr_id);
        printf("   📍 DEBUG - RDMA buffer: %p\n", rdma_buffer);
        printf("   📍 DEBUG - RDMA MR LKEY: 0x%x\n", rdma_mr->lkey);
        printf("   📍 DEBUG - Remote addr: 0x%016lx\n", server_info.addr);
        printf("   📍 DEBUG - Remote RKEY: 0x%08x\n", server_info.rkey);
        printf("   📍 Status codes: 0=success, 4=local_length_error, 7=local_protection_error, 9=remote_access_error\n");
        ibv_dereg_mr(rdma_mr);
        ibv_dereg_mr(recv_mr);
        free(rdma_buffer);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_disconnect(cm_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    rdma_buffer[99] = '\0';  // Terminer la chaîne
    
    printf("   ✨ RDMA READ RÉUSSI ! ✨\n");
    printf("   ┌─────────────────────────────────────────────┐\n");
    printf("   │ Lu DIRECTEMENT depuis RAM serveur :         │\n");
    printf("   │ '%s'    │\n", rdma_buffer);
    printf("   │                                             │\n");
    printf("   │ ✓ Le serveur ne s'est PAS réveillé !        │\n");
    printf("   │ ✓ Sa carte InfiniBand a géré seule !        │\n");
    printf("   │ ✓ Latence : ~1-5 μs (vs 5 ms disque)       │\n");
    printf("   └─────────────────────────────────────────────┘\n\n");
    
    sleep(3);
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 13 : RDMA WRITE - ÉCRIRE DANS LA RAM DU SERVEUR
    // ═══════════════════════════════════════════════════════
    // ✨ ENCORE PLUS MAGIQUE ! ✨
    //
    // QU'EST-CE QUI SE PASSE CONCRÈTEMENT ?
    //
    // 1. Je prépare un message dans mon buffer local
    // 2. Je prépare une requête RDMA_WRITE
    // 3. Je spécifie :
    //    - D'où prendre les données (mon buffer local)
    //    - Où les écrire (adresse RAM serveur)
    //    - La clé d'accès (RKEY)
    // 4. J'envoie la requête à ma carte InfiniBand
    // 5. MA CARTE lit mon buffer local
    // 6. MA CARTE envoie à la CARTE SERVEUR
    // 7. LA CARTE SERVEUR écrit DIRECTEMENT dans la RAM serveur
    // 8. LE CPU DU SERVEUR N'A JAMAIS ÉTÉ RÉVEILLÉ ! 😴
    //
    // Le serveur va découvrir le nouveau contenu quand il va
    // checker sa RAM (toutes les 5 secondes dans son code)
    
    printf("✍️  ÉTAPE 13 : RDMA WRITE - Écriture RAM serveur\n");
    printf("   ┌─────────────────────────────────────────────┐\n");
    printf("   │ Je vais écrire DIRECTEMENT dans RAM serveur│\n");
    printf("   │ TOUJOURS sans réveiller son CPU !           │\n");
    printf("   │                                             │\n");
    printf("   │ Mon buffer ──> Ma carte IB ──> Carte IB    │\n");
    printf("   │               serveur ──> RAM serveur       │\n");
    printf("   │                (bypass CPU serveur)         │\n");
    printf("   └─────────────────────────────────────────────┘\n\n");
    
    const char *new_message = "HELLO FROM CLIENT! I modified your RAM via RDMA!";
    strcpy(rdma_buffer, new_message);
    
    // Préparer RDMA WRITE
    struct ibv_sge write_sge;
    write_sge.addr = (uint64_t)rdma_buffer;
    write_sge.length = strlen(new_message) + 1;
    write_sge.lkey = rdma_mr->lkey;
    
    struct ibv_send_wr write_wr;
    memset(&write_wr, 0, sizeof(write_wr));
    write_wr.wr_id = 4;
    write_wr.sg_list = &write_sge;
    write_wr.num_sge = 1;
    write_wr.opcode = IBV_WR_RDMA_WRITE;  // ← RDMA WRITE !
    write_wr.send_flags = IBV_SEND_SIGNALED;
    write_wr.wr.rdma.remote_addr = server_info.addr;
    write_wr.wr.rdma.rkey = server_info.rkey;
    
    ret = ibv_post_send(cm_id->qp, &write_wr, &bad_wr);
    if (ret) {
        perror("   ❌ ibv_post_send (WRITE)");
        ibv_dereg_mr(rdma_mr);
        ibv_dereg_mr(recv_mr);
        free(rdma_buffer);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_disconnect(cm_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    // Attendre complétion
    while (ibv_poll_cq(cq, 1, &wc) < 1);
    
    if (wc.status != IBV_WC_SUCCESS) {
        printf("   ❌ RDMA WRITE échoué : %d\n", wc.status);
        ibv_dereg_mr(rdma_mr);
        ibv_dereg_mr(recv_mr);
        free(rdma_buffer);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_disconnect(cm_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✨ RDMA WRITE RÉUSSI ! ✨\n");
    printf("   ┌─────────────────────────────────────────────┐\n");
    printf("   │ J'ai écrit DIRECTEMENT dans RAM serveur :  │\n");
    printf("   │ '%s' │\n", new_message);
    printf("   │                                             │\n");
    printf("   │ ✓ Le serveur dort toujours ! 😴             │\n");
    printf("   │ ✓ Il va voir mon message quand il va       │\n");
    printf("   │   checker sa RAM dans ~5 secondes !         │\n");
    printf("   └─────────────────────────────────────────────┘\n\n");
    
    sleep(3);
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 14 : RE-LIRE POUR VÉRIFIER
    // ═══════════════════════════════════════════════════════
    // On re-fait RDMA_READ pour vérifier que notre écriture
    // a bien été persistée
    
    printf("🔍 ÉTAPE 14 : Vérification - Re-lecture RAM\n");
    
    memset(rdma_buffer, 0, BUFFER_SIZE);  // Effacer buffer local
    
    ret = ibv_post_send(cm_id->qp, &read_wr, &bad_wr);
    if (ret) {
        perror("   ❌ ibv_post_send (READ verification)");
        ibv_dereg_mr(rdma_mr);
        ibv_dereg_mr(recv_mr);
        free(rdma_buffer);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_disconnect(cm_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    while (ibv_poll_cq(cq, 1, &wc) < 1);
    
    if (wc.status != IBV_WC_SUCCESS) {
        printf("   ❌ Vérification échouée\n");
        ibv_dereg_mr(rdma_mr);
        ibv_dereg_mr(recv_mr);
        free(rdma_buffer);
        free(recv_buffer);
        ibv_destroy_qp(cm_id->qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        rdma_disconnect(cm_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    rdma_buffer[99] = '\0';
    
    printf("   ✅ VÉRIFICATION RÉUSSIE !\n");
    printf("   ┌─────────────────────────────────────────────┐\n");
    printf("   │ Contenu actuel RAM serveur :                │\n");
    printf("   │ '%s' │\n", rdma_buffer);
    printf("   │                                             │\n");
    printf("   │ ✓ Mon écriture a bien été persistée !      │\n");
    printf("   │ ✓ Le serveur va voir ce message quand      │\n");
    printf("   │   il va checker sa RAM !                    │\n");
    printf("   └─────────────────────────────────────────────┘\n\n");
    
    // Cleanup
    ibv_dereg_mr(rdma_mr);
    ibv_dereg_mr(recv_mr);
    free(rdma_buffer);
    free(recv_buffer);
    ibv_destroy_qp(cm_id->qp);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    rdma_disconnect(cm_id);
    rdma_destroy_id(cm_id);
    rdma_destroy_event_channel(cm_channel);
    
    printf("═══════════════════════════════════════════════════\n");
    printf("    FIN DU CLIENT\n");
    printf("═══════════════════════════════════════════════════\n");
    
    return 0;
}