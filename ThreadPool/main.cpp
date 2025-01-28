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