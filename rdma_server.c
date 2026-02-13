/*
 * ════════════════════════════════════════════════════════════════════
 * RDMA SERVER - Hello World InfiniBand
 * ════════════════════════════════════════════════════════════════════
 * 
 * CE QUE FAIT CE PROGRAMME :
 * 
 * 1. Alloue 1 MB de RAM
 * 2. Écrit "Hello from Server!" dedans
 * 3. EXPOSE cette RAM via InfiniBand
 * 4. Donne au client : adresse + clé d'accès (RKEY)
 * 5. DORT - ne touche plus jamais cette RAM
 * 
 * LE TRUC FOU :
 * → Le client va lire/écrire dans cette RAM
 * → Sans JAMAIS réveiller le CPU du serveur
 * → La carte InfiniBand gère tout !
 * 
 * C'est EXACTEMENT ce que fait InfiniSwap pour page-out/page-in
 * 
 * Compilation :
 *   gcc -Wall -g -o rdma_server rdma_server.c -lrdmacm -libverbs -lpthread
 * 
 * Utilisation :
 *   ./rdma_server
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <rdma/rdma_cma.h>

#define BUFFER_SIZE 1024*1024  // 1 MB de RAM à exposer

// Structure pour transmettre les infos RDMA au client
struct rdma_buffer_info {
    uint64_t addr;      // Adresse virtuelle de la RAM
    uint32_t rkey;      // Clé d'accès RDMA (Remote Key)
};

int main() {
    printf("═══════════════════════════════════════════════════\n");
    printf("    RDMA SERVER - HELLO WORLD INFINIBAND\n");
    printf("═══════════════════════════════════════════════════\n\n");
    
    // CRITICAL: Verrouiller la mémoire pour RDMA
    // Évite que le kernel ne "swap" la mémoire sur disque
    // Ce qui bloquerait l'HCA d'accéder à la RAM physique
    printf("🔒 Verrouillage mémoire pour RDMA...\n");
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("   ⚠️  mlockall échoué (non-critique, continue)");
    } else {
        printf("   ✅ Mémoire verrouillée pour RDMA\n\n");
    }
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 1 : ALLOUER LA RAM QU'ON VA EXPOSER
    // ═══════════════════════════════════════════════════════
    // CONCRÈTEMENT : malloc() alloue 1 MB dans notre espace mémoire
    // Cette RAM est normale pour l'instant (pas encore RDMA-accessible)
    
    printf("📦 ÉTAPE 1 : Allocation mémoire\n");
    printf("   Utilisons buffer statique (pré-alloué)...\n");
    
    // Buffer STATIQUE - plus stable pour RDMA, déjà en mémoire
    static char buffer[1024*1024] __attribute__((aligned(4096)));
    memset(buffer, 0, sizeof(buffer));
    strcpy(buffer, "Hello from Server! This is RDMA magic.");
    
    printf("   ✅ RAM allouée à l'adresse : %p\n", buffer);
    printf("   📝 Contenu initial : '%s'\n\n", buffer);
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 2 : CRÉER UN "RDMA EVENT CHANNEL"
    // ═══════════════════════════════════════════════════════
    // C'EST QUOI ?
    // → Un canal pour recevoir les événements RDMA
    // → Comme ouvrir un socket, mais pour RDMA
    // → Les événements : connexion, déconnexion, erreurs, etc.
    
    printf("🔌 ÉTAPE 2 : Création RDMA Event Channel\n");
    printf("   (Canal pour recevoir les événements RDMA)\n");
    
    struct rdma_event_channel *cm_channel = rdma_create_event_channel();
    if (!cm_channel) {
        perror("   ❌ rdma_create_event_channel");
        return 1;
    }
    
    printf("   ✅ Event channel créé\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 3 : CRÉER UN "RDMA CM ID"
    // ═══════════════════════════════════════════════════════
    // C'EST QUOI ?
    // → L'identifiant de connexion RDMA
    // → Équivalent d'un "socket file descriptor" en TCP
    // → Chaque connexion RDMA a son propre CM ID
    
    printf("🆔 ÉTAPE 3 : Création RDMA CM ID\n");
    printf("   (Identifiant de connexion - comme un socket)\n");
    
    struct rdma_cm_id *cm_id;
    int ret = rdma_create_id(cm_channel, &cm_id, NULL, RDMA_PS_TCP);
    if (ret) {
        perror("   ❌ rdma_create_id");
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ CM ID créé\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 4 : BIND SUR UNE ADRESSE
    // ═══════════════════════════════════════════════════════
    // CONCRÈTEMENT : Comme bind() pour TCP
    // → On dit "j'écoute sur le port 12345"
    // → N'importe quelle interface (INADDR_ANY)
    
    printf("📍 ÉTAPE 4 : Bind sur port 12345\n");
    printf("   (Comme bind() en TCP)\n");
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    addr.sin_addr.s_addr = INADDR_ANY;  // Toutes les interfaces
    
    ret = rdma_bind_addr(cm_id, (struct sockaddr *)&addr);
    if (ret) {
        perror("   ❌ rdma_bind_addr");
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Bind réussi sur 0.0.0.0:12345\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 5 : ÉCOUTER LES CONNEXIONS
    // ═══════════════════════════════════════════════════════
    // CONCRÈTEMENT : Comme listen() pour TCP
    // → On attend des connexions entrantes
    // → Backlog = 1 (une seule connexion à la fois)
    
    printf("👂 ÉTAPE 5 : Écoute des connexions\n");
    printf("   (Comme listen() en TCP)\n");
    
    ret = rdma_listen(cm_id, 1);
    if (ret) {
        perror("   ❌ rdma_listen");
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ En écoute sur port 12345\n\n");
    
    printf("═══════════════════════════════════════════════════\n");
    printf("    SERVEUR PRÊT - En attente du client...\n");
    printf("═══════════════════════════════════════════════════\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 6 : ACCEPTER LA CONNEXION CLIENT
    // ═══════════════════════════════════════════════════════
    // CONCRÈTEMENT : Comme accept() pour TCP
    // → On attend un événement RDMA_CM_EVENT_CONNECT_REQUEST
    // → Le client essaie de se connecter
    
    printf("⏳ ÉTAPE 6 : Attente connexion client...\n");
    
    struct rdma_cm_event *event;
    ret = rdma_get_cm_event(cm_channel, &event);
    if (ret) {
        perror("   ❌ rdma_get_cm_event");
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
        printf("   ❌ Événement inattendu : %d\n", event->event);
        rdma_ack_cm_event(event);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    struct rdma_cm_id *client_id = event->id;
    printf("   ✅ Client connecté !\n\n");
    rdma_ack_cm_event(event);
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 7 : CRÉER "PROTECTION DOMAIN" (PD)
    // ═══════════════════════════════════════════════════════
    // C'EST QUOI ?
    // → Une zone de sécurité pour tes ressources RDMA
    // → Toutes tes ressources (QP, MR) doivent être dans le même PD
    // → Comme un "namespace" pour isoler les ressources
    
    printf("🛡️  ÉTAPE 7 : Création Protection Domain\n");
    printf("   (Zone de sécurité pour ressources RDMA)\n");
    
    struct ibv_pd *pd = ibv_alloc_pd(client_id->verbs);
    if (!pd) {
        perror("   ❌ ibv_alloc_pd");
        rdma_destroy_id(client_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Protection Domain créé\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 8 : ENREGISTRER LA RAM (MEMORY REGISTRATION)
    // ═══════════════════════════════════════════════════════
    // ✨ C'EST L'ÉTAPE MAGIQUE ! ✨
    // 
    // QU'EST-CE QUI SE PASSE CONCRÈTEMENT ?
    // 
    // 1. Tu dis à la carte InfiniBand : "Cette zone RAM est à toi"
    // 2. La carte "pin" cette RAM en mémoire physique
    //    (l'OS ne peut plus la déplacer ou la swapper)
    // 3. La carte te donne une RKEY (Remote Key = clé d'accès)
    // 4. Avec cette RKEY, le client pourra accéder à cette RAM
    //
    // DROITS D'ACCÈS :
    // - IBV_ACCESS_LOCAL_WRITE  : le serveur peut écrire localement
    // - IBV_ACCESS_REMOTE_READ  : le client peut lire à distance
    // - IBV_ACCESS_REMOTE_WRITE : le client peut écrire à distance
    //
    // APRÈS CETTE ÉTAPE :
    // → La carte InfiniBand peut lire/écrire cette RAM
    // → SANS passer par le CPU du serveur
    // → C'est la MAGIE de RDMA !
    
    printf("✨ ÉTAPE 8 : Memory Registration (MAGIE RDMA)\n");
    printf("   ┌─────────────────────────────────────────────┐\n");
    printf("   │ On dit à la carte InfiniBand :              │\n");
    printf("   │ 'Cette RAM est à toi, tu peux y accéder     │\n");
    printf("   │  directement sans passer par le CPU !'      │\n");
    printf("   └─────────────────────────────────────────────┘\n\n");
    
    struct ibv_mr *mr = ibv_reg_mr(
        pd,                             // Protection Domain
        buffer,                         // Adresse de la RAM
        BUFFER_SIZE,                    // Taille (1 MB)
        IBV_ACCESS_LOCAL_WRITE |        // Serveur peut écrire
        IBV_ACCESS_REMOTE_READ |        // Client peut lire
        IBV_ACCESS_REMOTE_WRITE         // Client peut écrire
    );
    
    if (!mr) {
        perror("   ❌ ibv_reg_mr");
        ibv_dealloc_pd(pd);
        rdma_destroy_id(client_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ MAGIE ACCOMPLIE ! ✨\n");
    printf("   ┌─────────────────────────────────────────────┐\n");
    printf("   │ La carte InfiniBand peut maintenant :      │\n");
    printf("   │ • Lire cette RAM directement                │\n");
    printf("   │ • Écrire dans cette RAM directement         │\n");
    printf("   │ • SANS réveiller le CPU du serveur !        │\n");
    printf("   └─────────────────────────────────────────────┘\n\n");
    
    printf("   📊 Infos de la RAM enregistrée :\n");
    printf("      • Adresse virtuelle : %p\n", buffer);
    printf("      • RKEY (clé accès)  : 0x%x\n", mr->rkey);
    printf("      • LKEY (clé locale) : 0x%x\n\n", mr->lkey);
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 9 : CRÉER COMPLETION QUEUE (CQ)
    // ═══════════════════════════════════════════════════════
    // C'EST QUOI ?
    // → Une file d'attente pour les notifications
    // → Quand une opération RDMA se termine, un événement arrive ici
    // → Le CPU peut "poll" cette queue pour savoir si c'est fini
    
    printf("📨 ÉTAPE 9 : Création Completion Queue\n");
    printf("   (File pour notifications d'opérations RDMA)\n");
    
    struct ibv_cq *cq = ibv_create_cq(client_id->verbs, 16, NULL, NULL, 0);
    if (!cq) {
        perror("   ❌ ibv_create_cq");
        ibv_dereg_mr(mr);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(client_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Completion Queue créée\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 10 : CRÉER QUEUE PAIR (QP)
    // ═══════════════════════════════════════════════════════
    // C'EST QUOI ?
    // → Le "tuyau" par lequel passent les données RDMA
    // → Équivalent d'un socket TCP, mais pour RDMA
    // → 2 queues :
    //   - Send Queue : pour envoyer des données
    //   - Receive Queue : pour recevoir des données
    // → Type RC (Reliable Connection) = connexion fiable
    
    printf("🚰 ÉTAPE 10 : Création Queue Pair\n");
    printf("   (Le 'tuyau' RDMA - équivalent d'un socket)\n");
    
    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.send_cq = cq;               // CQ pour envois
    qp_attr.recv_cq = cq;               // CQ pour réceptions
    qp_attr.qp_type = IBV_QPT_RC;       // RC = Reliable Connection
    qp_attr.cap.max_send_wr = 16;       // Max 16 send en attente
    qp_attr.cap.max_recv_wr = 16;       // Max 16 recv en attente
    qp_attr.cap.max_send_sge = 1;       // 1 segment par send
    qp_attr.cap.max_recv_sge = 1;       // 1 segment par recv
    
    ret = rdma_create_qp(client_id, pd, &qp_attr);
    if (ret) {
        perror("   ❌ rdma_create_qp");
        ibv_destroy_cq(cq);
        ibv_dereg_mr(mr);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(client_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Queue Pair créée\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 11 : ACCEPTER LA CONNEXION
    // ═══════════════════════════════════════════════════════
    // CONCRÈTEMENT : On finalise la connexion avec le client
    // → On envoie notre "ACK" au client
    // → La connexion RDMA est maintenant établie
    
    printf("🤝 ÉTAPE 11 : Acceptation de la connexion\n");
    
    struct rdma_conn_param conn_param;
    memset(&conn_param, 0, sizeof(conn_param));
    
    ret = rdma_accept(client_id, &conn_param);
    if (ret) {
        perror("   ❌ rdma_accept");
        ibv_destroy_qp(client_id->qp);
        ibv_destroy_cq(cq);
        ibv_dereg_mr(mr);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(client_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Connexion acceptée\n");
    
    // Attendre l'événement ESTABLISHED
    ret = rdma_get_cm_event(cm_channel, &event);
    if (ret || event->event != RDMA_CM_EVENT_ESTABLISHED) {
        printf("   ❌ Échec établissement connexion\n");
        rdma_ack_cm_event(event);
        ibv_destroy_qp(client_id->qp);
        ibv_destroy_cq(cq);
        ibv_dereg_mr(mr);
        ibv_dealloc_pd(pd);
        rdma_destroy_id(client_id);
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
        return 1;
    }
    
    printf("   ✅ Connexion ÉTABLIE\n\n");
    rdma_ack_cm_event(event);
    
    // ═══════════════════════════════════════════════════════
    // INITIALISER LE QP : POST_RECV DUMMY
    // ═══════════════════════════════════════════════════════
    // Les QP doivent avoir au moins une opération RECV postée
    // pour être complètement initialisés et prêts pour les
    // opérations RDMA distantes (READ/WRITE)
    
    struct ibv_sge recv_sge_dummy;
    recv_sge_dummy.addr = (uint64_t)buffer;
    recv_sge_dummy.length = sizeof(struct rdma_buffer_info);
    recv_sge_dummy.lkey = mr->lkey;
    
    struct ibv_recv_wr recv_wr_dummy, *bad_recv_wr;
    memset(&recv_wr_dummy, 0, sizeof(recv_wr_dummy));
    recv_wr_dummy.wr_id = 0;
    recv_wr_dummy.sg_list = &recv_sge_dummy;
    recv_wr_dummy.num_sge = 1;
    
    ret = ibv_post_recv(client_id->qp, &recv_wr_dummy, &bad_recv_wr);
    if (ret) {
        perror("   ⚠️  Dummy POST_RECV échoué (continue)");
    }
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 12 : ENVOYER LES INFOS AU CLIENT
    // ═══════════════════════════════════════════════════════
    // ON ENVOIE QUOI ?
    // → L'adresse virtuelle de la RAM
    // → La RKEY (clé d'accès)
    //
    // AVEC CES 2 INFOS, LE CLIENT POURRA :
    // → Faire RDMA_READ pour lire la RAM
    // → Faire RDMA_WRITE pour écrire dans la RAM
    // → SANS réveiller le CPU du serveur !
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 12 : ENVOYER LES INFOS AU CLIENT
    // ═══════════════════════════════════════════════════════
    // ON ENVOIE QUOI ?
    // → L'adresse virtuelle de la RAM
    // → La RKEY (clé d'accès)
    //
    // AVEC CES 2 INFOS, LE CLIENT POURRA :
    // → Faire RDMA_READ pour lire la RAM
    // → Faire RDMA_WRITE pour écrire dans la RAM
    // → SANS réveiller le CPU du serveur !
    
    printf("📤 ÉTAPE 12 : Envoi des infos au client\n");
    
    // Placer les infos au DÉBUT du buffer (plus sûr)
    struct rdma_buffer_info *info = (struct rdma_buffer_info *)buffer;
    info->addr = (uint64_t)buffer;
    info->rkey = mr->rkey;

    printf("   ┌─────────────────────────────────────────────┐\n");
    printf("   │ INFORMATIONS ENVOYÉES AU CLIENT :           │\n");
    printf("   ├─────────────────────────────────────────────┤\n");
    printf("   │ Adresse RAM : 0x%016lx          │\n", info->addr);
    printf("   │ RKEY        : 0x%08x                    │\n", info->rkey);
    printf("   │ Info addr   : 0x%016lx (start)     │\n", (uint64_t)info);
    printf("   │ Buffer addr : 0x%016lx                │\n", (uint64_t)buffer);
    printf("   │ MR LKEY     : 0x%08x                    │\n", mr->lkey);
    printf("   │                                             │\n");
    printf("   │ Le client peut maintenant :                 │\n");
    printf("   │ • RDMA_READ  → lire cette RAM               │\n");
    printf("   │ • RDMA_WRITE → écrire dans cette RAM        │\n");
    printf("   │ • Sans JAMAIS réveiller mon CPU ! 😴        │\n");
    printf("   └─────────────────────────────────────────────┘\n\n");

    // Préparer la requête d'envoi (depuis le buffer qui est enregistré)
    struct ibv_sge sge;
    sge.addr = (uint64_t)info;  // ← Envoyer depuis le DÉBUT du buffer
    sge.length = sizeof(struct rdma_buffer_info);
    sge.lkey = mr->lkey;

    struct ibv_send_wr send_wr, *bad_wr;
    memset(&send_wr, 0, sizeof(send_wr));
    send_wr.wr_id = 1;
    send_wr.sg_list = &sge;
    send_wr.num_sge = 1;
    send_wr.opcode = IBV_WR_SEND;
    send_wr.send_flags = IBV_SEND_SIGNALED;

    ret = ibv_post_send(client_id->qp, &send_wr, &bad_wr);
    if (ret) {
        perror("   ❌ ibv_post_send");
        return 1;
    }

    // Attendre la complétion
    struct ibv_wc wc;
    while (ibv_poll_cq(cq, 1, &wc) < 1);

    if (wc.status != IBV_WC_SUCCESS) {
        printf("   ❌ Envoi échoué (status: %d)\n", wc.status);
        return 1;
    }

    printf("   ✅ Infos envoyées au client\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 13 : ATTENDRE SIGNAL DU CLIENT AVANT D'ENVOYER
    // ═══════════════════════════════════════════════════════
    // Le client va nous envoyer un dummy message pour signaler
    // qu'il a posté son RECV et est prêt à recevoir
    
    printf("📥 ÉTAPE 13 : Attente signal client avant envoi données...\n");
    
    struct ibv_sge sge_signal;
    sge_signal.addr = (uint64_t)buffer;
    sge_signal.length = 1;  // Juste 1 byte
    sge_signal.lkey = mr->lkey;
    
    struct ibv_recv_wr recv_signal_wr, *bad_recv_signal_wr;
    memset(&recv_signal_wr, 0, sizeof(recv_signal_wr));
    recv_signal_wr.wr_id = 100;
    recv_signal_wr.sg_list = &sge_signal;
    recv_signal_wr.num_sge = 1;
    
    ret = ibv_post_recv(client_id->qp, &recv_signal_wr, &bad_recv_signal_wr);
    if (ret) {
        perror("   ❌ ibv_post_recv (signal)");
        return 1;
    }
    
    // Attendre le signal du client
    while (ibv_poll_cq(cq, 1, &wc) < 1);
    
    if (wc.status != IBV_WC_SUCCESS) {
        printf("   ❌ Signal échoué (status: %d)\n", wc.status);
        return 1;
    }
    
    printf("   ✅ Signal reçu - le client est prêt\n\n");
    
    // ═══════════════════════════════════════════════════════
    // ÉTAPE 14 : ENVOYER LE CONTENU DU BUFFER AU CLIENT
    // ═══════════════════════════════════════════════════════
    printf("📤 ÉTAPE 14 : Envoi contenu RAM au client\n");
    
    struct ibv_sge sge_data;
    sge_data.addr = (uint64_t)buffer;
    sge_data.length = 100;  // Envoyer 100 octets
    sge_data.lkey = mr->lkey;
    
    struct ibv_send_wr send_wr_data, *bad_wr_data;
    memset(&send_wr_data, 0, sizeof(send_wr_data));
    send_wr_data.wr_id = 2;
    send_wr_data.sg_list = &sge_data;
    send_wr_data.num_sge = 1;
    send_wr_data.opcode = IBV_WR_SEND;
    send_wr_data.send_flags = IBV_SEND_SIGNALED;
    
    ret = ibv_post_send(client_id->qp, &send_wr_data, &bad_wr_data);
    if (ret) {
        perror("   ❌ ibv_post_send (données)");
        return 1;
    }
    
    // Attendre complétion
    while (ibv_poll_cq(cq, 1, &wc) < 1);
    
    if (wc.status != IBV_WC_SUCCESS) {
        printf("   ❌ Envoi données échoué (status: %d)\n", wc.status);
        return 1;
    }
    
    printf("   ✅ Données envoyées au client\n\n");

    // ═══════════════════════════════════════════════════════
    // ÉTAPE 14 : DORMIR - LE SERVEUR NE FAIT PLUS RIEN !
    // ═══════════════════════════════════════════════════════
    // À PARTIR DE MAINTENANT :
    // → Le serveur dort
    // → Le client a reçu les données
    // → La carte InfiniBand gère tout
    // → Le CPU du serveur reste endormi
    // → C'est la MAGIE de RDMA !
    
    printf("═══════════════════════════════════════════════════\n");
    printf("    SERVEUR EN MODE VEILLE 😴\n");
    printf("═══════════════════════════════════════════════════\n\n");
    
    printf("Le serveur dort maintenant... 💤\n\n");
    printf("Pendant ce temps :\n");
    printf("  → Le client va lire/écrire dans la RAM\n");
    printf("  → La carte InfiniBand gère tout seule\n");
    printf("  → Mon CPU reste endormi\n");
    printf("  → C'est la MAGIE de RDMA ! ✨\n\n");
    
    printf("Je vais checker ma RAM toutes les 5 secondes...\n\n");
    
    for (int i = 0; i < 20; i++) {
        sleep(5);
        printf("[%3ds] Contenu RAM : '%s'\n", (i+1)*5, buffer);
        
        // Si le contenu a changé, le client a écrit !
        if (strstr(buffer, "CLIENT")) {
            printf("       👆 LE CLIENT A ÉCRIT ICI ! Mon CPU dormait ! 🎉\n");
        }
    }
    
    printf("\n═══════════════════════════════════════════════════\n");
    printf("    FIN DU SERVEUR\n");
    printf("═══════════════════════════════════════════════════\n");
    
    // Cleanup
    ibv_dereg_mr(mr);
    ibv_destroy_qp(client_id->qp);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    rdma_destroy_id(client_id);
    rdma_destroy_id(cm_id);
    rdma_destroy_event_channel(cm_channel);
    
    return 0;
}