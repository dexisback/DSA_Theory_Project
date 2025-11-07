/*
 * DSA Traffic Management Project
 * - adjacency-list graph, Dijkstra (min-heap)
 * - time-dependent waiting at traffic lights
 * - exports GraphViz DOT and an interactive Leaflet map (map_india.html)
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#define MAX 50
#define INF 999999

// ========== STRUCTURES ==========
typedef struct {
    int red;
    int green;
    int yellow;
} TrafficLight;

typedef struct Edge {
    int to;
    int weight;
    struct Edge *next;
} Edge;

typedef struct {
    int vertices;
    Edge *adj[MAX];          // adjacency list
    TrafficLight lights[MAX];
    char names[MAX][20];
    double lat[MAX];         // NEW: latitude
    double lon[MAX];         // NEW: longitude
} Graph;

// For min-heap priority queue
typedef struct {
    int v;
    int dist;
} HeapNode;

typedef struct {
    int size;
    int pos[MAX];   // position of vertex in heap array for decreaseKey
    HeapNode *arr[MAX];
} MinHeap;

// ========== FUNCTION DECLARATIONS ==========
void initGraph(Graph *g);
void addEdge(Graph *g, int u, int v, int w);
void displayGraph(Graph *g);
void loadGraphFromFile(Graph *g, const char *filename);
void saveGraphToFile(Graph *g, const char *filename);
int getWaitingTime(TrafficLight light, int arrivalTime);
void dijkstra(Graph *g, int src, int dest);
void writeGraphViz(Graph *g, const char *filename);
void exportLeafletMap(Graph *g, int *path, int path_len, const char *filename);

// Min-heap functions
MinHeap *createMinHeap(int capacity);
void swapHeapNode(HeapNode **a, HeapNode **b);
void minHeapify(MinHeap *h, int idx);
bool isEmpty(MinHeap *h);
HeapNode *extractMin(MinHeap *h);
void decreaseKey(MinHeap *h, int v, int dist);
bool isInMinHeap(MinHeap *h, int v);
void freeMinHeap(MinHeap *h);

// Utility
Edge *newEdge(int to, int weight);
void freeGraph(Graph *g);

// ========== IMPLEMENTATIONS ==========

void initGraph(Graph *g) {
    g->vertices = 0;
    for (int i = 0; i < MAX; i++) g->adj[i] = NULL, g->lat[i]=0.0, g->lon[i]=0.0;
}

// Create a new edge node
Edge *newEdge(int to, int weight) {
    Edge *e = (Edge *)malloc(sizeof(Edge));
    e->to = to;
    e->weight = weight;
    e->next = NULL;
    return e;
}

// Add undirected edge
void addEdge(Graph *g, int u, int v, int w) {
    if (u < 0 || u >= g->vertices || v < 0 || v >= g->vertices) {
        printf("Invalid edge indices: %d - %d\n", u, v);
        return;
    }
    Edge *e1 = newEdge(v, w);
    e1->next = g->adj[u];
    g->adj[u] = e1;

    Edge *e2 = newEdge(u, w);
    e2->next = g->adj[v];
    g->adj[v] = e2;
}

// Print adjacency list (readable)
void displayGraph(Graph *g) {
    printf("\nCity Map (Adjacency List):\n");
    for (int i = 0; i < g->vertices; i++) {
        printf("%d (%s) -> ", i, g->names[i]);
        Edge *p = g->adj[i];
        while (p) {
            printf("[%d,%d] ", p->to, p->weight);
            p = p->next;
        }
        printf("\n");
    }
}

// Save graph (edge list). Format:
// V
// name R G Y lat lon   (V lines)
// E
// u v w                (E lines, undirected written once with u<v)
void saveGraphToFile(Graph *g, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) { perror("saveGraphToFile fopen"); return; }
    fprintf(fp, "%d\n", g->vertices);
    for (int i = 0; i < g->vertices; i++) {
        fprintf(fp, "%s %d %d %d %.6f %.6f\n",
                g->names[i],
                g->lights[i].red, g->lights[i].green, g->lights[i].yellow,
                g->lat[i], g->lon[i]);
    }

    // collect edges once
    int edges_count = 0;
    for (int u = 0; u < g->vertices; u++) {
        Edge *p = g->adj[u];
        while (p) {
            if (u < p->to) edges_count++;
            p = p->next;
        }
    }
    fprintf(fp, "%d\n", edges_count);
    for (int u = 0; u < g->vertices; u++) {
        Edge *p = g->adj[u];
        while (p) {
            if (u < p->to)
                fprintf(fp, "%d %d %d\n", u, p->to, p->weight);
            p = p->next;
        }
    }
    fclose(fp);
    printf("City data saved to %s\n", filename);

    // also write DOT
    writeGraphViz(g, "graphviz.dot");
    printf("GraphViz DOT exported to graphviz.dot\n");
}

// Load graph from file. Supports new format (name R G Y lat lon) and
// falls back to the older format (name R G Y) if lat/lon are absent.
void loadGraphFromFile(Graph *g, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("File not found! Starting fresh.\n");
        initGraph(g);
        return;
    }

    initGraph(g);
    int v;
    if (fscanf(fp, "%d", &v) != 1) { fclose(fp); initGraph(g); return; }
    g->vertices = v;

    // consume newline (file pointer is at start of junction lines)
    for (int i = 0; i < g->vertices; i++) {
        // try to read 6 tokens
        char name[20];
        int R,G,Y;
        double la, lo;
        int read6 = fscanf(fp, "%19s %d %d %d %lf %lf", name, &R, &G, &Y, &la, &lo);
        if (read6 == 6) {
            strncpy(g->names[i], name, sizeof(g->names[i]) - 1);
            g->names[i][sizeof(g->names[i]) - 1] = '\0';
            g->lights[i].red=R; g->lights[i].green=G; g->lights[i].yellow=Y;
            g->lat[i]=la; g->lon[i]=lo;
        } else {
            // fallback: try reading a line and parsing name R G Y
            clearerr(fp);
            fseek(fp, 0, SEEK_CUR); // portability no-op
            char line[256];
            int ok = 0;
            while (fgets(line, sizeof(line), fp)) {
                    if (sscanf(line, "%19s %d %d %d", name, &R, &G, &Y) == 4) {
                    strncpy(g->names[i], name, sizeof(g->names[i]) - 1);
                    g->names[i][sizeof(g->names[i]) - 1] = '\0';
                    g->lights[i].red=R; g->lights[i].green=G; g->lights[i].yellow=Y;
                    g->lat[i]=0.0; g->lon[i]=0.0; // default if old file
                    ok = 1; break;
                }
            }
            if (!ok) {
                // bad line, set defaults
                snprintf(g->names[i], sizeof(g->names[i]), "J%d", i);
                g->lights[i].red=10; g->lights[i].green=5; g->lights[i].yellow=2;
                g->lat[i]=0.0; g->lon[i]=0.0;
            }
        }
    }

    int edges_count = 0;
    if (fscanf(fp, "%d", &edges_count) != 1) edges_count = 0;
    for (int i = 0; i < edges_count; i++) {
        int u, vv, w;
        if (fscanf(fp, "%d %d %d", &u, &vv, &w) != 3) break;
        Edge *e1 = newEdge(vv, w);
        e1->next = g->adj[u];
        g->adj[u] = e1;

        Edge *e2 = newEdge(u, w);
        e2->next = g->adj[vv];
        g->adj[vv] = e2;
    }

    fclose(fp);
    printf("City data loaded from %s\n", filename);
}

// Write GraphViz DOT file (undirected graph)
void writeGraphViz(Graph *g, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) { perror("writeGraphViz fopen"); return; }
    fprintf(fp, "graph City {\n");
    fprintf(fp, "  overlap=false;\n");
    fprintf(fp, "  splines=true;\n");
    // Node labels show name and traffic timing
    for (int i = 0; i < g->vertices; i++) {
        fprintf(fp, "  n%d [label=\"%s\\nR:%d G:%d Y:%d\"];\n", i, g->names[i],
                g->lights[i].red, g->lights[i].green, g->lights[i].yellow);
    }
    // Edges: ensure each undirected edge printed once (u<v)
    for (int u = 0; u < g->vertices; u++) {
        Edge *p = g->adj[u];
        while (p) {
            if (u < p->to) {
                fprintf(fp, "  n%d -- n%d [label=\"%d\"];\n", u, p->to, p->weight);
            }
            p = p->next;
        }
    }
    fprintf(fp, "}\n");
    fclose(fp);
}

// Compute waiting time at a traffic light given arrivalTime
int getWaitingTime(TrafficLight light, int arrivalTime) {
    int cycle = light.red + light.green + light.yellow;
    if (cycle <= 0) return 0;
    int t = arrivalTime % cycle;
    if (t < light.green) return 0;          // green window
    return cycle - t;                        // wait till next green
}

// ========== MIN HEAP IMPLEMENTATION ==========
MinHeap *createMinHeap(int capacity) {
    MinHeap *h = (MinHeap *)malloc(sizeof(MinHeap));
    h->size = 0;
    for (int i = 0; i < capacity; i++) h->arr[i] = NULL, h->pos[i] = -1;
    return h;
}

void swapHeapNode(HeapNode **a, HeapNode **b) {
    HeapNode *t = *a; *a = *b; *b = t;
}

void minHeapify(MinHeap *h, int idx) {
    int smallest = idx;
    int left = 2*idx + 1;
    int right = 2*idx + 2;
    if (left < h->size && h->arr[left]->dist < h->arr[smallest]->dist)
        smallest = left;
    if (right < h->size && h->arr[right]->dist < h->arr[smallest]->dist)
        smallest = right;
    if (smallest != idx) {
        h->pos[h->arr[smallest]->v] = idx;
        h->pos[h->arr[idx]->v] = smallest;
        swapHeapNode(&h->arr[smallest], &h->arr[idx]);
        minHeapify(h, smallest);
    }
}

bool isEmpty(MinHeap *h) { return h->size == 0; }

// extract min node
HeapNode *extractMin(MinHeap *h) {
    if (isEmpty(h)) return NULL;
    HeapNode *root = h->arr[0];
    HeapNode *lastNode = h->arr[h->size - 1];
    h->arr[0] = lastNode;
    h->pos[lastNode->v] = 0;
    h->pos[root->v] = -1;
    h->size--;
    minHeapify(h, 0);
    return root;
}

// decrease key for vertex v to new dist
void decreaseKey(MinHeap *h, int v, int dist) {
    int i = h->pos[v];
    if (i == -1) return;
    h->arr[i]->dist = dist;
    while (i && h->arr[i]->dist < h->arr[(i-1)/2]->dist) {
        h->pos[h->arr[i]->v] = (i-1)/2;
        h->pos[h->arr[(i-1)/2]->v] = i;
        swapHeapNode(&h->arr[i], &h->arr[(i-1)/2]);
        i = (i-1)/2;
    }
}

bool isInMinHeap(MinHeap *h, int v) { return h->pos[v] != -1; }

void freeMinHeap(MinHeap *h) {
    for (int i = 0; i < h->size; i++) if (h->arr[i]) free(h->arr[i]);
    free(h);
}

// ========== LEAFLET MAP EXPORT ==========
static void js_escape_name(const char *src, char *dst, size_t dstsz) {
    // very small escaper: replace " with ' and backslash with slash
    size_t j=0;
    for (size_t i=0; src[i] && j+1<dstsz; i++) {
        char c = src[i];
        if (c=='"' || c=='\\') c = '\'';
        dst[j++] = c;
    }
    dst[j] = '\0';
}

void exportLeafletMap(Graph *g, int *path, int path_len, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) { perror("exportLeafletMap fopen"); return; }

    fprintf(fp,
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>City Map - India</title>"
    "<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'/>"
    "<style>html,body,#map{height:100%%;margin:0;} .edge-label{background:transparent;border:none;font-weight:600;}</style>"
    "</head><body><div id='map'></div>"
    "<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>"
    "<script src='https://cdnjs.cloudflare.com/ajax/libs/leaflet.polylinedecorator/1.7.0/leaflet.polylineDecorator.min.js'></script>"
    "<script>\n");

    // Center over India
    fprintf(fp,
    "var map = L.map('map').setView([22.5937, 78.9629], 5);\n"
    "L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {"
    "maxZoom: 18, attribution: '&copy; OpenStreetMap contributors'}).addTo(map);\n");

    // Nodes array
    fprintf(fp, "var nodes = [\n");
    for (int i = 0; i < g->vertices; i++) {
        char safe[64]; js_escape_name(g->names[i], safe, sizeof(safe));
        fprintf(fp, "  {id:%d, name:\"%s\", lat:%lf, lon:%lf}%s\n",
                i, safe, g->lat[i], g->lon[i], (i+1<g->vertices)? ",":"");
    }
    fprintf(fp, "];\n");

    // Edges array (u<v only)
    fprintf(fp, "var edges = [\n");
    int first = 1;
    for (int u = 0; u < g->vertices; u++) {
        for (Edge *p = g->adj[u]; p; p = p->next) {
            int v = p->to;
            if (u < v) {
                fprintf(fp, "  %s{u:%d, v:%d, w:%d}\n", first?"":",", u, v, p->weight);
                first = 0;
            }
        }
    }
    fprintf(fp, "];\n");

    // Shortest path indices
    fprintf(fp, "var sp = [");
    for (int i = 0; i < path_len; i++) {
        fprintf(fp, "%d%s", path[i], (i+1<path_len)? ",":"");
    }
    fprintf(fp, "];\n");

    // Draw markers
    fprintf(fp,
    "nodes.forEach(n=>{\n"
    "  var m = L.marker([n.lat, n.lon]).addTo(map);\n"
    "  m.bindPopup('<b>'+n.name+'</b>');\n"
    "});\n"
    "function pt(id){ let n = nodes.find(x=>x.id===id); return [n.lat, n.lon]; }\n");

    // Draw all edges
    fprintf(fp,
    "edges.forEach(e=>{\n"
    "  var line = L.polyline([pt(e.u), pt(e.v)], {weight:3, opacity:0.6}).addTo(map);\n"
    "  var a=pt(e.u), b=pt(e.v);\n"
    "  var mid=[(a[0]+b[0])/2,(a[1]+b[1])/2];\n"
    "  L.marker(mid,{opacity:0}).addTo(map)\n"
    "    .bindTooltip(String(e.w),{permanent:true,direction:'center',className:'edge-label'});\n"
    "});\n");

    // Draw shortest path (with arrowheads)
    fprintf(fp,
    "if (sp.length>1){\n"
    "  var coords=[]; for (var i=0;i<sp.length;i++){ coords.push(pt(sp[i])); }\n"
    "  var spLine = L.polyline(coords, {color:'red', weight:6, opacity:0.9}).addTo(map);\n"
    "  spLine.bindPopup('Shortest Path');\n"
    "  try {\n"
    "    var decorator = L.polylineDecorator(spLine, {patterns: [\n"
    "      {offset: '5%%', repeat: '15%%', symbol: L.Symbol.arrowHead({pixelSize: 10, polygon: false, pathOptions: {stroke: true, color: 'red'}})}\n"
    "    ]}).addTo(map);\n"
    "  } catch(e) { console.warn('PolylineDecorator not available', e); }\n"
    "  map.fitBounds(coords, {padding:[40,40]});\n"
    "} else { // if no path, fit all nodes\n"
    "  var all=[]; nodes.forEach(n=>all.push([n.lat,n.lon]));\n"
    "  if (all.length>0) map.fitBounds(all, {padding:[40,40]});\n"
    "}\n");

    fprintf(fp, "</script></body></html>");
    fclose(fp);
    printf("Interactive India map exported to %s\n", filename);
}

// ========== DIJKSTRA (time-dependent) ==========
void dijkstra(Graph *g, int src, int dest) {
    int n = g->vertices;
    if (src < 0 || src >= n || dest < 0 || dest >= n) {
        printf("Invalid source/destination indices.\n");
        return;
    }

    int dist[MAX];
    int parent[MAX];
    for (int i = 0; i < n; i++) {
        dist[i] = INF;
        parent[i] = -1;
    }
    MinHeap *h = createMinHeap(n);


    // initialize heap nodes
    for (int v = 0; v < n; v++) {
        HeapNode *hn = (HeapNode *)malloc(sizeof(HeapNode));
        hn->v = v;
        hn->dist = INF;
        h->arr[h->size] = hn;
        h->pos[v] = h->size;
        h->size++;
    }

    // set src dist = 0
    dist[src] = 0;
    h->arr[h->pos[src]]->dist = 0;
    decreaseKey(h, src, 0);

    while (!isEmpty(h)) {
        HeapNode *hn = extractMin(h);
        if (!hn) break;
        int u = hn->v;
        int du = hn->dist;
        free(hn);

        if (du == INF) break;
        if (u == dest) break;

        Edge *p = g->adj[u];
        while (p) {
            int v = p->to;
            int w = p->weight;
            if (h->pos[v] != -1) {
                int arrival = dist[u] + w;
                int wait = getWaitingTime(g->lights[v], arrival);
                int newDist = dist[u] + w + wait;
                if (newDist < dist[v]) {
                    dist[v] = newDist;
                    parent[v] = u;
                    int idx = h->pos[v];
                    if (idx != -1 && h->arr[idx]) {
                        h->arr[idx]->dist = newDist;
                        decreaseKey(h, v, newDist);
                    }
                }
            }
            p = p->next;
        }
    }

    // Build path
    if (dist[dest] == INF) {
        printf("\nNo path found from %s to %s\n", g->names[src], g->names[dest]);
        // still export map without path
        int dummy[1]={0};
        exportLeafletMap(g, dummy, 0, "map_india.html");
    } else {
        printf("\nShortest Time from %s to %s = %d units\n",
               g->names[src], g->names[dest], dist[dest]);
        printf("\nPath Travel Summary:\n");
        int path[MAX];
        int idx = 0;
        for (int v = dest; v != -1; v = parent[v])
            path[idx++] = v;
        for (int i = idx - 1; i >= 0; i--) {
            printf("%s", g->names[path[i]]);
            if (i != 0) printf(" -> ");
        }
        printf("\nTotal Time Taken: %d units\n", dist[dest]);

        // Export map with highlighted shortest path
        // Note: path[] is currently reversed, but we printed in forward order.
        // Build forward array for the map:
        int forward[MAX];
        for (int i = 0; i < idx; i++) forward[i] = path[idx-1-i];
        exportLeafletMap(g, forward, idx, "map_india.html");
    }

    freeMinHeap(h);
}

// Free adjacency list memory
void freeGraph(Graph *g) {
    for (int i = 0; i < g->vertices; i++) {
        Edge *p = g->adj[i];
        while (p) {
            Edge *tmp = p;
            p = p->next;
            free(tmp);
        }
        g->adj[i] = NULL;
    }
}

// ========== MAIN PROGRAM ==========
int main() {
    Graph city;
    initGraph(&city);
    int choice;
    const char *filename = "city_data.txt";

    printf("=== SMART TRAFFIC MANAGEMENT SYSTEM (AdjList + PQ + India Map) ===\n");
    loadGraphFromFile(&city, filename);

    do {
        printf("\nMenu:\n");
        printf("1. Add Junctions & Roads\n");
        printf("2. Display City Map (Adjacency List)\n");
        printf("3. Find Shortest Path\n");
        printf("4. Save & Exit\n");
        printf("5. Export Interactive Map (India)\n");
        printf("Enter your choice: ");
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            choice = 0;
        }

        if (choice == 1) {
            freeGraph(&city);

            printf("Enter number of junctions (max %d): ", MAX);
            int vcount;
            scanf("%d", &vcount);
            if (vcount < 1 || vcount > MAX) {
                printf("Invalid number; must be 1..%d\n", MAX);
                continue;
            }
            city.vertices = vcount;
            for (int i = 0; i < city.vertices; i++) {
                printf("\nJunction %d name: ", i);
                scanf("%19s", city.names[i]);
                printf("Enter traffic light timings (Red Green Yellow) for %s: ", city.names[i]);
                scanf("%d %d %d", &city.lights[i].red,
                      &city.lights[i].green, &city.lights[i].yellow);
                printf("Enter latitude and longitude for %s (e.g., 28.6139 77.2090): ", city.names[i]);
                scanf("%lf %lf", &city.lat[i], &city.lon[i]);
                city.adj[i] = NULL;
            }

            int e;
            printf("Enter number of roads: ");
            scanf("%d", &e);
            printf("Enter roads (u v distance) each in a new line:\n");
            for (int i = 0; i < e; i++) {
                int u, v, w;
                scanf("%d %d %d", &u, &v, &w);
                addEdge(&city, u, v, w);
            }
        }

        else if (choice == 2) {
            displayGraph(&city);
        }

        else if (choice == 3) {
            int s, d;
            printf("Enter source and destination index: ");
            scanf("%d %d", &s, &d);
            dijkstra(&city, s, d);
            printf("Open map_india.html to see the route highlighted.\n");
        }

        else if (choice == 4) {
            saveGraphToFile(&city, filename);
            printf("Exiting...\n");
        }

        else if (choice == 5) {
            // export without a shortest path (just all edges/nodes)
            int dummy[1] = {0};
            exportLeafletMap(&city, dummy, 0, "map_india.html");
            printf("Open map_india.html to view the current city network.\n");
        }

        else {
            if (choice != 0)
                printf("Invalid choice. Try again.\n");
        }

    } while (choice != 4);

    freeGraph(&city);
    return 0;
}