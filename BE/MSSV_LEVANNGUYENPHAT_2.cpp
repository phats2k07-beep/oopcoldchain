#include<bits/stdc++.h>
using namespace std;
class SACH{
    private:
    string MS;
    string TEN;
    float DG;  
    static int sosach=0;
    static float tong=0;
    public:
    SACH() {
        MS="";  
        TEN="";
        DG=0.0;
    }
    void nhapSACH(string MS,string TEN,float DG){
        this->MS=MS;
        this->TEN=TEN;
        this->DG=DG;
        tong=tong+DG;
        sosach++;
    } 
    void inSACH(){
        cout<<"Mã Sách:"<<MS"   "<<"Tên Sách:"<<TEN"   "<<<<"Điểm Đánh Giá:"<<DG"    ";
    }
   float trungbinh=tong/sosach;
   bool operator <(SACH s){
        return this->DG < s.DG;
    }
    ~SACH() {
       sosach--;
    }

};
class SACH_GIAO_TRINH:SACH{
    private:
    float NOI_DUNG;
    float TINH_HUU_ICH;
    public:
    SACH_GIAO_TRINH() {
        NOI_DUNG=0.0;
        TINH_HUU_ICH=0.0;
    }
    void nhapSACH(string MS,string TEN,float NOI_DUNG,float TINH_HUU_ICH){
        this->MS=MS;
        this->TEN=TEN;
        this->NOI_DUNG=NOI_DUNG;
        this->TINH_HUU_ICH=TINH_HUU_ICH;
        
    }
    DG=0.4*NOI_DUNG+0.6*TINH_HUU_ICH;
}
class SACH_THAM_KHAO:SACH{
    private:
    float DO_SAU;
    float TINH_THUC_TIEN;
    public:
    void nhapSACH(string MS,string TEN,float DO_SAU,float TINH_THUC_TIEN){
        this->MS=MS;
        this->TEN=TEN;
        this->DO_SAU=DO_SAU;
        this->TINH_THUC_TIEN=TINH_THUC_TIEN;
    }
    DG=0.5*DO_SAU+0.5*TINH_THUC_TIEN;
}
int main(){
    SACH s[n];
    cin>>n;
    for(int i=0;i<n;i++){
        for(int j=0;j<n;j++){
            if(s[i].DG<s[j].DG){
                swap(s[i],s[j]);
            }
        }
}
cout << "STT" << "    " << "Tên Sách" << "    " << "Điểm Đánh Giá" << "    " << endl;
for(int i=0;i<n;i++){
    cout<<i<<"    ";
    s[i].inSACH();
}
