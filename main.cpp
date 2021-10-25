extern "C" {
    void sta_ker(void);
}

int main()
{
    __asm__ volatile ("cpsid f");
    sta_ker();
    return 0;
}
