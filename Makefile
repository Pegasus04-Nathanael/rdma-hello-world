# ════════════════════════════════════════════════════════════════
# MAKEFILE - RDMA Hello World
# ════════════════════════════════════════════════════════════════
#
# Ce Makefile compile les programmes RDMA client/serveur
#
# Utilisation :
#   make        → Compile tout
#   make clean  → Efface les exécutables
#   make server → Compile juste le serveur
#   make client → Compile juste le client

CC = gcc
CFLAGS = -Wall -g -O2
LDFLAGS = -lrdmacm -libverbs -lpthread

# Compilateur : gcc
# -Wall : Affiche tous les warnings
# -g : Inclut infos de debug
# -O2 : Optimisation niveau 2
#
# Librairies :
# -lrdmacm : RDMA Connection Manager
# -libverbs : InfiniBand Verbs (API de base)
# -lpthread : Threads POSIX (requis par libverbs)

.PHONY: all clean server client

all: rdma_server rdma_client
	@echo ""
	@echo "═══════════════════════════════════════════════════"
	@echo "    COMPILATION RÉUSSIE ! ✅"
	@echo "═══════════════════════════════════════════════════"
	@echo ""
	@echo "Fichiers générés :"
	@echo "  • rdma_server  (à lancer sur node0)"
	@echo "  • rdma_client  (à lancer sur node1)"
	@echo ""
	@echo "Prochaines étapes :"
	@echo "  1. Sur node0 : ./rdma_server"
	@echo "  2. Sur node1 : ./rdma_client <ip_node0>"
	@echo ""

server: rdma_server

client: rdma_client

rdma_server: rdma_server.c
	@echo "Compilation rdma_server..."
	$(CC) $(CFLAGS) -o rdma_server rdma_server.c $(LDFLAGS)
	@echo "✅ rdma_server compilé"

rdma_client: rdma_client.c
	@echo "Compilation rdma_client..."
	$(CC) $(CFLAGS) -o rdma_client rdma_client.c $(LDFLAGS)
	@echo "✅ rdma_client compilé"

clean:
	@echo "Nettoyage..."
	rm -f rdma_server rdma_client
	@echo "✅ Fichiers effacés"