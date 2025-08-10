int main() {

    int i = 0;

    // while loop
    while (i < 3) {
        i = i + 1;
    }

    // for loop
    for (int j = 0; j < 3; j = j + 1) {
        i = i + j;
    }

    // until loop (executes until condition becomes true)
    until (i > 10) { i = i + 2; }

    // do-while loop
    do {
        i = i + 1;
    } while (i < 15);

    // if-else condition
    if (i < 20) {
        i = i + 5;
    } else {
        i = i - 5;
    }

    // switch-case condition
    switch (i) {
        case 10:
            i = 100;
            break;
        case 15:
            i = 200;
            break;
        default:
            i = 300;
            break;  
    }

    // break and continue
    for (int k = 0; k < 5; k++) {
        if (k == 2) {
            break; // exit loop
        }
        if (k == 1) {
            continue; // skip this iteration
        }
        i = i + k;
    }

    // goto

    if (i < 50) {
        goto label; // jump to label
    }

    label:
    i = i + 10; // label target
    if (i > 100) {
        goto end; // jump to end
    }

    end:
    // Final value of i
    printf("Final value of i: %d\n", i);

}
