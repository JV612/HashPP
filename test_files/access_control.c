class TestClass {
    private:
        int private_var;
    protected:
        int protected_var;
    public:
        int public_var;
        
        TestClass() {
            private_var = 1;   // Should work - inside class
            protected_var = 2; // Should work - inside class
            public_var = 3;    // Should work - inside class
        }
        
        void test_method() {
            private_var = 10;   // Should work - inside class
            protected_var = 20; // Should work - inside class 
            public_var = 30;    // Should work - inside class
        }
};

int main() {
    class TestClass obj;
    class TestClass* ptr;
    ptr = &obj;
    
    // These should work (public access)
    obj.public_var = 100;
    ptr->public_var = 200;
    
    // These should fail (private access from outside)
    obj.private_var = 400;     // ERROR: private
    ptr->private_var = 500;    // ERROR: private
    
    // These should fail (protected access from outside)  
    obj.protected_var = 600;   // ERROR: protected
    ptr->protected_var = 700;  // ERROR: protected
    
    return 0;
}