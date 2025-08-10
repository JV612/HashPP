struct Node {
    int value;
    struct Node *next;
};

int factorial(int n) {
    if (n <= 1) {
        return 1;
    } else {
        return n * factorial(n - 1);
    }
}

int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n-1) + fibonacci(n-2);
}

void ProcessNode(struct Node *node) {
    if(node != nullptr) {
        node->value += 10;
    }
}

int main () {

    Node *root = nullptr;
    Node **pp = &root;
    root->value = 10;
    root = (Node*) malloc(sizeof(Node));
    
    root->next = nullptr;
    ProcessNode(root);

    int fact = factorial(5);
    int fib = fibonacci(5);

}