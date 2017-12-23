#pragma once

#include <exception>
#include <stdio.h>

/*
	例外の追加方法
	1. デフォルトメッセージの宣言を記述(kexception.h)
	2. 例外のtypedefを記述(kexception.h)
	3. デフォルトメッセージの定義を記述(kexception_impl.h)
	4. 例外テンプレートの明示的なインスタンス化を記述(kexception_impl.h)
*/

namespace utl {

namespace DefExceptMes {

const char* MesK = "一般エラー";
const char* MesIO = "I/Oエラーです";

}

struct printError;

// Root of All exceptions
class KException : public std::exception
{
	friend printError;
public:
	explicit KException::KException(const char* _Message, const char* _FileName, int _Line) :
		std::exception(_Message),
		File(_FileName),
		Line(_Line)
	{
	}

	explicit KException::KException(const char* _FileName, int _Line) :
		std::exception(DefExceptMes::MesK),
		File(_FileName),
		Line(_Line)
	{
	}

	explicit KException::KException(const char* _Message) :
		std::exception(_Message),
		File("Unknown"),
		Line(0)
	{
	}

	explicit KException::KException() :
		std::exception(DefExceptMes::MesK),
		File("Unknown"),
		Line(0)
	{
	}

	explicit KException::KException(const std::exception& stdexcept) :
		std::exception(stdexcept.what()),
		File("Unknown"),
		Line(0)
	{
	}

	explicit KException::KException(const std::exception& stdexcept, const char* _FileName, int _Line) :
		std::exception(stdexcept.what()),
		File(_FileName),
		Line(_Line)
	{
	}

	virtual KException::~KException()
	{
	}

	const char* KException::getMessage() const
	{
		return std::exception::what();
	}

	const char* KException::getFileName() const
	{
		return File;
	}

	int KException::getLine() const
	{
		return Line;
	}

	static void KException::setPrintHandler(void(*_printHandler)(const KException&))
	{
		printHandler = _printHandler;
	}


protected:
	const char* File;
	int Line;

private:
	static void(*printHandler)(const KException& This);
};

static void kexceptionDefaultPrint(const KException& This)
{
	printf("%s(%d):%s\n", This.getFileName(), This.getLine(), This.getMessage());
}

void(*KException::printHandler)(const KException& This) = kexceptionDefaultPrint;

struct printError
{
	void operator ()(const std::exception& source)
	{
		const KException* pKe = dynamic_cast<const KException*>(&source);
		if (pKe != NULL) {
			KException::printHandler(*pKe);
		}
		else {
			KException::printHandler(KException(source));
		}
	}
};

void operator|(const std::exception& source, printError& func)
{
	func(source);
}

// デバッグ用例外作成支援
#ifdef _DEBUG
#define DEFMES __FILE__, __LINE__
#else
#define DEFMES
#endif

#ifdef _DEBUG
#define MES(mes) (mes), __FILE__, __LINE__
#else
#define MES(mes) (mes)
#endif

// 例外定義をするためのテンプレート
template <class Base, const char*& DefMes>
class EcpDef : public Base
{
public:
	explicit EcpDef(const char* _FileName, int _Line) :
		KException(DefMes, _FileName, _Line)
	{
	}
	explicit EcpDef() :
		KException(DefMes)
	{
	}
	explicit EcpDef(const char* _Message, const char* _FileName, int _Line) :
		KException(_Message, _FileName, _Line)
	{
	}
	explicit EcpDef(const char* _Message) :
		KException(_Message)
	{
	}
};

typedef EcpDef<KException, DefExceptMes::MesIO> IOException;

// 例外テンプレートの明示的なインスタンス化
template class EcpDef<KException, DefExceptMes::MesIO>;

}
