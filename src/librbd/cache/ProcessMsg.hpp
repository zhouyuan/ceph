#ifndef PROCESS_MSG_HPP
#define PROCESS_MSG_HPP
#include <string>

// TODO : namespace conflict...TODO 
// TODO : rename
class ProcessMsg0 {
  ProcessMsg0(const ProcessMsg0& other);
  const ProcessMsg0& operator=(const ProcessMsg0& other);

 protected:
  virtual void process(int r, std::string msg) = 0;


 public:
  ProcessMsg0() {}
  virtual ~ProcessMsg0() {}

  // user interface 
  virtual void process_msg(int r, std::string msg) {
    process(r, msg);
    delete this;
  }

};

template <typename T>
struct LambdaProcessFunction : public ProcessMsg0 {
  T t;
  LambdaProcessFunction(T &&t) : t(std::forward<T>(t)) {}

  void process(int r, std::string msg) override {
    t(r, msg);
  }
};

template <typename T>
LambdaProcessFunction<T> *make_lambda_process_function(T &&t) {
  return new LambdaProcessFunction<T>(std::move(t));
}

#endif
