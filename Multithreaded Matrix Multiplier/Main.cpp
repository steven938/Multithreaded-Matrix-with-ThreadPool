// Multithreaded Matrix Multiplier.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <vector>
#include <iostream>
#include <minmax.h>
#include <random>
#include <chrono>
#include "ThreadPool.h"

using namespace std;
typedef vector<vector<int>> matrix;

void printMatrix(matrix& m) {
    for (int i = 0; i < m.size(); i++)
        for (int j = 0; j < m[0].size(); j++)
            // Prints ' ' if j != n-1 else prints '\n'           
            cout << m[i][j] << " \n"[j == m[0].size() -1];
}



void randColumn(matrix& m, int i, int dimension) {
    for (int j = 0; j < dimension; j++) {
        int rand_num = rand();
        m[i][j] = rand_num;
        //cout << i << " " << j << endl;
    }
}

matrix randomSquareMatrix(int dimension, ThreadPool& tp) {
    matrix m(dimension, vector<int>(dimension, 0));
    //auto start = chrono::high_resolution_clock::now();
    for (int i = 0; i < dimension; i++) {
        tp.enqueue(randColumn, ref(m), i, dimension);
    }
    tp.wait_for_completion();
    //auto end = chrono::high_resolution_clock::now();
    //auto duration = chrono::duration_cast<chrono::microseconds>(end - start);
    //cout << "duration: " << duration.count() << endl;
    return m;
}

void helperMultiply(matrix& A, matrix& B, matrix& res, int dimension, int i) {
    for (int j = 0; j < dimension; j++) {
        for (int k = 0; k < dimension; k++) {
            res[i][j] += A[i][k] + B[j][k];
            // locking of res matrix is not required because each thread writes to a unique locations
            // locking of A B matrices not required because read access only
        }
    }
}

void multiplyMatrix(matrix& A, matrix& B, matrix& res, int dimension, ThreadPool& tp) {
    auto start = chrono::high_resolution_clock::now();
    for (int i = 0; i < dimension; i++) {
        //helperMultiply(A, B, res, dimension, i);
        tp.enqueue(helperMultiply, ref(A), ref(B), ref(res), dimension, i);
    }
    tp.wait_for_completion();
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end - start);
    cout << duration.count() << endl;
}


int main()
{
    int n = max(thread::hardware_concurrency(), 4); // dynamically generate threadpool
    int dimension = 500;
    ThreadPool tp(n);
    matrix m1 = randomSquareMatrix(dimension, ref(tp));
    matrix m2 = randomSquareMatrix(dimension, ref(tp));
    
    matrix result(dimension, vector<int>(dimension, 0));

    multiplyMatrix(m1, m2, result, dimension, tp);
    
    cout << result[dimension - 1][dimension - 1] << endl;

    
}

// When using thread pool, there is no overhead in creating in thread
// therefore granularity does not affect performance
// to maximize parallelism, we want to ensure that all threads are in use
// each matrix operation is separated by dimension (partitioned by the i th dimension)
// to maximize concurrency, dimension should be large