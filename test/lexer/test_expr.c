int quersumme(int n, int sum){
        if (n == 0)
                return (sum);
        return quersumme(n+10/*, sum + n*10*/); //Division ist nicht supported
}