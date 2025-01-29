//#include "Thread_Pool.h"
//
//class Add : public Task
//{
//public:
//	Add() { }
//	~Add() { }
//	Any run() override
//	{
//		return 0x12345678;
//	}
//};
//
//std::shared_ptr<Add> dd = std::make_shared<Add>();
//
//Result test()
//{
//	return Result(dd, true);
//}
//
//int main()
//{
//	test();
//	
//	dd->exec();
//
//	
//
//	return 0;
//}
#include <iostream>
using namespace std;

class Test
{
public:
	Test(int a = 10) : ma(a) { cout << "Test()" << endl; }
	~Test() { cout << "~Test()" << endl; }
	Test(const Test &t) : ma(t.ma) { cout << "Test(const Test &)" << endl; }
	Test& operator=(const Test &t)
	{
		cout << "operator=(const Test &)" << endl;
		ma = t.ma;
		return *this;
	}

private:
	int ma;
};